#include <core_net/vm/signals.hpp>
#include <chrono>
#include <csignal>
#include <thread>
#include <iostream>

#include <catch2/catch.hpp>

struct test_exception {};

TEST_CASE("Testing signals", "[invoke_with_signal_handler]") {
   core_net::vm::growable_allocator alloc(1024);
   core_net::vm::wasm_allocator wa;
   // Trigger a real SIGSEGV by writing to the wasm_allocator's read-only
   // guard page, so that si_addr falls within memory_range. Using
   // std::raise(SIGSEGV) does not set si_addr, causing the signal handler
   // to fall through to SIG_DFL on AArch64.
   auto mem_span = wa.get_span();
   volatile char* guard_page = reinterpret_cast<volatile char*>(mem_span.data());
   bool okay = false;
   try {
      core_net::vm::invoke_with_signal_handler([guard_page]() {
         *guard_page = 0; // write to PROT_READ guard page → SIGSEGV
      }, [](int sig) {
         throw test_exception{};
      }, alloc, &wa);
   } catch(test_exception&) {
      okay = true;
   }
   CHECK(okay);
}

TEST_CASE("Testing throw", "[signal_handler_throw]") {
   core_net::vm::growable_allocator alloc(1024);
   core_net::vm::wasm_allocator wa;
   CHECK_THROWS_AS(core_net::vm::invoke_with_signal_handler([](){
      core_net::vm::throw_<core_net::vm::wasm_exit_exception>( "Exiting" );
   }, [](int){}, alloc, &wa), core_net::vm::wasm_exit_exception);
}

static volatile sig_atomic_t sig_handled;

static void handle_signal(int sig) {
   sig_handled = 42 + sig;
}

static void handle_signal_sigaction(int sig, siginfo_t* info, void* uap) {
   sig_handled = 142 + sig;
}

TEST_CASE("Test signal handler forwarding", "[signal_handler_forward]") {
   // reset backup signal handlers
   auto guard = core_net::vm::scope_guard{[]{
      std::signal(SIGSEGV, SIG_DFL);
      std::signal(SIGBUS, SIG_DFL);
      std::signal(SIGFPE, SIG_DFL);
      core_net::vm::setup_signal_handler_impl(); // This is normally only called once
   }};
   {
      std::signal(SIGSEGV, &handle_signal);
      std::signal(SIGBUS, &handle_signal);
      std::signal(SIGFPE, &handle_signal);
      core_net::vm::setup_signal_handler_impl();
      sig_handled = 0;
      std::raise(SIGSEGV);
      CHECK(sig_handled == 42 + SIGSEGV);
#ifndef __linux__
      sig_handled = 0;
      std::raise(SIGBUS);
      CHECK(sig_handled == 42 + SIGBUS);
#endif
      sig_handled = 0;
      std::raise(SIGFPE);
      CHECK(sig_handled == 42 + SIGFPE);
   }
   {
      struct sigaction sa;
      sa.sa_sigaction = &handle_signal_sigaction;
      sigemptyset(&sa.sa_mask);
      sa.sa_flags = SA_NODEFER | SA_SIGINFO;
      sigaction(SIGSEGV, &sa, nullptr);
      sigaction(SIGBUS, &sa, nullptr);
      sigaction(SIGFPE, &sa, nullptr);
      core_net::vm::setup_signal_handler_impl();
      sig_handled = 0;
      std::raise(SIGSEGV);
      CHECK(sig_handled == 142 + SIGSEGV);
#ifndef __linux__
      sig_handled = 0;
      std::raise(SIGBUS);
      CHECK(sig_handled == 142 + SIGBUS);
#endif
      sig_handled = 0;
      std::raise(SIGFPE);
      CHECK(sig_handled == 142 + SIGFPE);
   }
}
