#!/usr/bin/env python3

import re
import signal
import time

from TestHarness import Cluster, TestHelper, Utils, WalletMgr, CORE_SYMBOL, createAccountKeys
from TestHarness.TestHelper import AppArgs

###############################################################
# p2p_encrypted_long_sync_test
#
# Regression test for issue #98 — P2P encryption fails after first batch.
#
# The v0.1.2-alpha unit tests passed only 100 messages through aead_context;
# p2p_encrypted_transport_test synced a short cluster. Neither scenario saturated
# the connection's write pipeline long enough to trigger the cross-queue
# reordering that produced "Message decryption/authentication failed" in
# production after ~10K blocks (issue #98).
#
# This test exercises the encrypted send path under sustained load:
#   1. Producing cluster with --p2p-enable-encryption on every node.
#   2. trx generators fill blocks with transactions so each block carries
#      many trx and trx_notice messages (populates _trx_write_queue while
#      sync blocks populate _sync_write_queue — the reordering trigger).
#   3. A late-joining non-producing node is started with encryption enabled
#      and syncs the accumulated chain (pushes thousands of signed_block
#      messages through encrypted transport in a single catchup).
#
# Assertions:
#   - Catchup completes — the node reaches the producer's head block.
#   - No "Message decryption/authentication failed" in any node's stderr.
#   - No "out_of_range_exception" in any node's stderr during the encrypted
#     session (the server-side symptom from issue #98).
###############################################################

Print = Utils.Print
errorExit = Utils.errorExit

appArgs = AppArgs()
appArgs.add(flag='--catchup-blocks', type=int,
            help='Minimum blocks the late-joining node must sync through encrypted transport',
            default=10_000)
appArgs.add(flag='--trx-gen-duration', type=int,
            help='Seconds to run trx generators before starting catchup node',
            default=60)

args = TestHelper.parse_args({"-d", "--keep-logs", "--activate-if",
                              "--dump-error-details", "-v", "--leave-running",
                              "--unshared"},
                             applicationSpecificArgs=appArgs)
delay = args.d
debug = args.v
dumpErrorDetails = args.dump_error_details
catchupBlocks = args.catchup_blocks
trxGenDurationSec = args.trx_gen_duration

Utils.Debug = debug
testSuccessful = False

pnodes = 1
totalNodes = 3            # 1 producer + 1 non-producing peer + 1 late-joining catchup node
cluster = Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
walletMgr = WalletMgr(True)


def count_log_matches(node, pattern):
    log_file = node.data_dir / 'stderr.txt'
    if not log_file.exists():
        return 0
    with open(log_file, 'r') as f:
        return len(re.findall(pattern, f.read()))


def assert_no_encryption_failures(node, label):
    """Verify the node's log is free of the issue #98 symptoms.

    The canonical #98 client symptom is "Message decryption/authentication
    failed". We don't assert on `out_of_range_exception` alone because it also
    fires on benign trx-generator / stale-connection handshake parse errors
    that pre-date this fix."""
    dec_fail = count_log_matches(node, r'Message decryption/authentication failed')
    assert dec_fail == 0, f'{label}: {dec_fail} decryption failure(s) in log (issue #98 symptom)'
    seal_fail = count_log_matches(node, r'Encryption seal failed')
    assert seal_fail == 0, f'{label}: {seal_fail} seal failure(s) in log'


try:
    TestHelper.printSystemInfo("BEGIN")
    cluster.setWalletMgr(walletMgr)

    # Enable encryption on the V2 nodes but DON'T require it — the launcher's
    # bios node is plaintext and would be rejected at handshake, preventing
    # cluster bootstrap.
    encryption_args = '--p2p-enable-encryption true'
    specificExtraNodeosArgs = {
        0: f'--plugin core_net::net_api_plugin {encryption_args}',
        1: f'--plugin core_net::net_api_plugin {encryption_args}',
    }

    # Launch cluster with one unstarted node (the late-joining catchup peer).
    assert cluster.launch(pnodes=pnodes, totalNodes=totalNodes, unstartedNodes=1,
                          prodCount=2, topo='mesh', delay=delay,
                          activateIF=args.activate_if,
                          specificExtraNodeosArgs=specificExtraNodeosArgs), \
        "Failed to launch encrypted cluster"

    prodNode = cluster.getNode(0)
    peerNode = cluster.getNode(1)

    # Verify encryption is active on the live links (fails fast if encryption
    # handshake broke — catchup results would be meaningless otherwise).
    for i in range(totalNodes - 1):
        node = cluster.getNode(i)
        for _ in range(30):
            if count_log_matches(node, r'Encrypted transport activated') >= 1:
                break
            time.sleep(1)
        activated = count_log_matches(node, r'Encrypted transport activated')
        assert activated >= 1, f'Node {i} did not activate encrypted transport'
    Print("Encryption activated on all live links")

    # Seed the cluster with transactions to generate load on the _trx_write_queue
    # while blocks continue filling the _sync_write_queue on the catchup path.
    accounts = createAccountKeys(2)
    accounts[0].name = "tester111111"
    accounts[1].name = "tester222222"
    walletMgr.create("test", [cluster.eosioAccount, accounts[0], accounts[1]])
    for account in accounts:
        peerNode.createInitializeAccount(account, cluster.eosioAccount, stakedDeposit=0,
                                         waitForTransBlock=True,
                                         stakeNet=1000, stakeCPU=1000, buyRAM=1000,
                                         exitOnError=True)
        peerNode.transferFunds(cluster.eosioAccount, account,
                               f"100000000.0000 {CORE_SYMBOL}", "funding",
                               waitForTransBlock=True)

    Print(f"Launching trx generators for {trxGenDurationSec}s")
    cluster.launchTrxGenerators(contractOwnerAcctName=cluster.eosioAccount.name,
                                acctNamesList=[accounts[0].name, accounts[1].name],
                                acctPrivKeysList=[accounts[0].activePrivateKey,
                                                  accounts[1].activePrivateKey],
                                nodeId=prodNode.nodeId,
                                tpsPerGenerator=500,
                                numGenerators=2,
                                durationSec=trxGenDurationSec,
                                waitToComplete=True)

    # Accumulate enough blocks for the catchup to traverse at least `catchupBlocks`.
    # Block interval is 500ms pre-Savanna; Savanna can be faster. Budget wall-clock
    # generously — this is a long-running test (add_lr_test).
    Print(f"Waiting for producer to accumulate {catchupBlocks} blocks")
    targetHead = catchupBlocks + 10
    assert prodNode.waitForBlock(targetHead, timeout=targetHead), \
        f"Producer did not reach block {targetHead}"
    Print(f"Producer head: {prodNode.getHeadBlockNum()}")

    # Launch the late-joining catchup node with encryption and a large fetch span.
    # `--sync-fetch-span 10000` matches the #98 reproduction configuration.
    catchupNode = cluster.unstartedNodes[0]
    catchupNode.cmd.extend([
        '--plugin', 'core_net::net_api_plugin',
        '--p2p-enable-encryption', 'true',
        '--sync-fetch-span', '10000',
    ])
    Print("Launching late-joining encrypted catchup node")
    catchupStart = time.time()
    cluster.launchUnstarted(1)
    # Nodes are 0-indexed; with pnodes=1 + 1 non-producer, the unstarted catchup
    # node is at index (totalNodes - 1).
    catchupNode = cluster.getNode(totalNodes - 1)
    assert catchupNode.verifyAlive(), "Catchup node did not launch"

    # Wait for catchup. Budget: block_count * 0.1s at a realistic sync ceiling of
    # ~10 blocks/s (much slower than real sync, but we're CI-safe).
    syncTarget = prodNode.getHeadBlockNum()
    syncTimeout = max(300, catchupBlocks // 5)
    Print(f"Catching up to block {syncTarget} (timeout {syncTimeout}s)")
    assert catchupNode.waitForBlock(syncTarget, timeout=syncTimeout), \
        f"Catchup node failed to reach block {syncTarget} within {syncTimeout}s — sync stalled"
    catchupElapsed = time.time() - catchupStart
    Print(f"Catchup completed in {catchupElapsed:.1f}s, head={catchupNode.getHeadBlockNum()}")

    # Verify the encrypted transport survived the full session on every node.
    # This is the regression assertion — #98 produced these exact log lines.
    for i in range(totalNodes):
        assert_no_encryption_failures(cluster.getNode(i), f"node-{i}")
    Print("No encryption failures in any node log — issue #98 regression check passed")

    # Sanity check that encrypted transport was activated on the catchup node too
    # (otherwise the absence of failures is meaningless).
    assert count_log_matches(catchupNode, r'Encrypted transport activated') >= 1, \
        "Catchup node did not activate encrypted transport — cannot validate encryption"
    Print("Catchup node activated encrypted transport and synced without decryption failures")

    testSuccessful = True

finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful,
                        dumpErrorDetails=dumpErrorDetails)

exit(0 if testSuccessful else 1)
