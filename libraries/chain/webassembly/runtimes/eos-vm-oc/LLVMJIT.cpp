/*
The EOS VM Optimized Compiler was created in part based on WAVM
https://github.com/WebAssembly/wasm-jit-prototype
subject the following:

Copyright (c) 2016-2019, Andrew Scheidecker
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
* Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
* Neither the name of WAVM nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "LLVMJIT.h"
#include "LLVMEmitIR.h"

#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/RTDyldMemoryManager.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/Core.h"

#if LLVM_VERSION_MAJOR < 12
#include "llvm/ExecutionEngine/Orc/LambdaResolver.h"
#include "llvm/ExecutionEngine/Orc/NullResolver.h"
#endif

#if LLVM_VERSION_MAJOR >= 12
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#endif

#include "llvm/Analysis/Passes.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/SymbolSize.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Utils.h"
#include <memory>
#include <unistd.h>
#include <fstream>

#include "llvm/Support/LEB128.h"

#if LLVM_VERSION_MAJOR == 7
namespace llvm { namespace orc {
	using LegacyRTDyldObjectLinkingLayer = RTDyldObjectLinkingLayer;
	template<typename A, typename B>
	using LegacyIRCompileLayer = IRCompileLayer<A, B>;
}}
#endif

#define DUMP_UNOPTIMIZED_MODULE 0
#define VERIFY_MODULE 1
#define DUMP_OPTIMIZED_MODULE 0
#define PRINT_DISASSEMBLY 0

#if PRINT_DISASSEMBLY
#include "llvm-c/Disassembler.h"
static void disassembleFunction(U8* bytes,Uptr numBytes)
{
	LLVMDisasmContextRef disasmRef = LLVMCreateDisasm(llvm::sys::getProcessTriple().c_str(),nullptr,0,nullptr,nullptr);

	U8* nextByte = bytes;
	Uptr numBytesRemaining = numBytes;
	while(numBytesRemaining)
	{
		char instructionBuffer[256];
		const Uptr numInstructionBytes = LLVMDisasmInstruction(
			disasmRef,
			nextByte,
			numBytesRemaining,
			reinterpret_cast<Uptr>(nextByte),
			instructionBuffer,
			sizeof(instructionBuffer)
			);
		if(numInstructionBytes == 0 || numInstructionBytes > numBytesRemaining)
			break;
		numBytesRemaining -= numInstructionBytes;
		nextByte += numInstructionBytes;

		printf("\t\t%s\n",instructionBuffer);
	};

	LLVMDisasmDispose(disasmRef);
}
#endif

namespace core_net { namespace chain { namespace eosvmoc {

namespace LLVMJIT
{
	llvm::TargetMachine* targetMachine = nullptr;

	// Allocates memory for the LLVM object loader.
	struct UnitMemoryManager : llvm::RTDyldMemoryManager
	{
		UnitMemoryManager() {}
		virtual ~UnitMemoryManager() override
		{}

		void registerEHFrames(U8* addr, U64 loadAddr,uintptr_t numBytes) override {}
		void deregisterEHFrames() override {}

		virtual bool needsToReserveAllocationSpace() override { return true; }
		virtual void reserveAllocationSpace(uintptr_t numCodeBytes,U32 codeAlignment,uintptr_t numReadOnlyBytes,U32 readOnlyAlignment,uintptr_t numReadWriteBytes,U32 readWriteAlignment) override {
			code = std::make_unique<std::vector<uint8_t>>(numCodeBytes + numReadOnlyBytes + numReadWriteBytes);
			ptr = code->data();
		}
		virtual U8* allocateCodeSection(uintptr_t numBytes,U32 alignment,U32 sectionID,llvm::StringRef sectionName) override
		{
			return get_next_code_ptr(numBytes, alignment);
		}
		virtual U8* allocateDataSection(uintptr_t numBytes,U32 alignment,U32 sectionID,llvm::StringRef SectionName,bool isReadOnly) override
		{
			if(SectionName == ".eh_frame") {
				dumpster.resize(numBytes);
				return dumpster.data();
			}
			if(SectionName == ".stack_sizes") {
				return stack_sizes.emplace_back(numBytes).data();
			}
			WAVM_ASSERT_THROW(isReadOnly);

			return get_next_code_ptr(numBytes, alignment);
		}

		virtual bool finalizeMemory(std::string* ErrMsg = nullptr) override {
			code->resize(ptr - code->data());
			return true;
		}

		std::unique_ptr<std::vector<uint8_t>> code;
		uint8_t* ptr;

		std::vector<uint8_t> dumpster;
		std::list<std::vector<uint8_t>> stack_sizes;

		U8* get_next_code_ptr(uintptr_t numBytes, U32 alignment) {
			WAVM_ASSERT_THROW(alignment <= alignof(std::max_align_t));
			uintptr_t p = (uintptr_t)ptr;
			p += alignment - 1LL;
			p &= ~(alignment - 1LL);
			uint8_t* this_section = (uint8_t*)p;
			ptr = this_section + numBytes;

			return this_section;
		}

		UnitMemoryManager(const UnitMemoryManager&) = delete;
		void operator=(const UnitMemoryManager&) = delete;
	};

#if LLVM_VERSION_MAJOR >= 12
	// ORCv2 JIT compilation unit (LLVM 12+)
	struct JITModule
	{
		JITModule() {
			ES.setErrorReporter([](llvm::Error Err) {
				std::string errStr;
				llvm::raw_string_ostream errOS(errStr);
				errOS << Err;
				std::ofstream("/tmp/oc_compile_error.log", std::ios::app)
					<< "  ORCv2 ERROR: " << errStr << std::endl;
			});
			objectLayer = std::make_unique<llvm::orc::RTDyldObjectLinkingLayer>(ES,[this]() {
									return std::unique_ptr<llvm::RuntimeDyld::MemoryManager>(
										std::make_unique<MemoryManagerForwarder>(*this));
							  });
			objectLayer->setProcessAllSections(true);
			objectLayer->setNotifyLoaded(
				[this](llvm::orc::MaterializationResponsibility &R, const llvm::object::ObjectFile &Obj,
				       const llvm::RuntimeDyld::LoadedObjectInfo &o) {
					for(auto symbolSizePair : llvm::object::computeSymbolSizes(Obj)) {
						auto symbol = symbolSizePair.first;
						auto name = symbol.getName();
						auto address = symbol.getAddress();
						if(symbol.getType() && symbol.getType().get() == llvm::object::SymbolRef::ST_Function && name && address) {
							Uptr loadedAddress = Uptr(*address);
							auto symbolSection = symbol.getSection();
							if(symbolSection)
								loadedAddress += (Uptr)o.getSectionLoadAddress(*symbolSection.get());
							Uptr functionDefIndex;
							if(getFunctionIndexFromExternalName(name->data(),functionDefIndex))
								function_to_offsets[functionDefIndex] = loadedAddress-(uintptr_t)unitmemorymanager->code->data();
#if PRINT_DISASSEMBLY
							disassembleFunction((U8*)loadedAddress, symbolSizePair.second);
#endif
						} else if(symbol.getType() && symbol.getType().get() == llvm::object::SymbolRef::ST_Data && name && *name == getTableSymbolName()) {
							Uptr loadedAddress = Uptr(*address);
							auto symbolSection = symbol.getSection();
							if(symbolSection)
								loadedAddress += (Uptr)o.getSectionLoadAddress(*symbolSection.get());
							table_offset = loadedAddress-(uintptr_t)unitmemorymanager->code->data();
						}
					}
				});
			compileLayer = std::make_unique<llvm::orc::IRCompileLayer>(ES, *objectLayer,
				std::make_unique<llvm::orc::SimpleCompiler>(*targetMachine));
		}

		void compile(llvm::Module* llvmModule);

		std::shared_ptr<UnitMemoryManager> unitmemorymanager = std::make_shared<UnitMemoryManager>();

		std::map<unsigned, uintptr_t> function_to_offsets;
		std::vector<uint8_t> final_pic_code;
		uintptr_t table_offset = 0;

		~JITModule()
		{
		}
	private:
		// Thin forwarder that delegates to the shared UnitMemoryManager.
		// ORCv2 RTDyldObjectLinkingLayer wants a factory that returns unique_ptr,
		// but we need all allocations to land in the same UnitMemoryManager so we
		// can collect the emitted code afterward.
		struct MemoryManagerForwarder : llvm::RTDyldMemoryManager {
			JITModule& owner;
			MemoryManagerForwarder(JITModule& o) : owner(o) {}
			void registerEHFrames(U8* addr, U64 loadAddr, uintptr_t numBytes) override {
				owner.unitmemorymanager->registerEHFrames(addr, loadAddr, numBytes);
			}
			void deregisterEHFrames() override {
				owner.unitmemorymanager->deregisterEHFrames();
			}
			bool needsToReserveAllocationSpace() override {
				return owner.unitmemorymanager->needsToReserveAllocationSpace();
			}
			void reserveAllocationSpace(uintptr_t numCodeBytes, U32 codeAlignment,
			                            uintptr_t numReadOnlyBytes, U32 readOnlyAlignment,
			                            uintptr_t numReadWriteBytes, U32 readWriteAlignment) override {
				owner.unitmemorymanager->reserveAllocationSpace(numCodeBytes, codeAlignment,
				                                                numReadOnlyBytes, readOnlyAlignment,
				                                                numReadWriteBytes, readWriteAlignment);
			}
			U8* allocateCodeSection(uintptr_t numBytes, U32 alignment, U32 sectionID, llvm::StringRef sectionName) override {
				return owner.unitmemorymanager->allocateCodeSection(numBytes, alignment, sectionID, sectionName);
			}
			U8* allocateDataSection(uintptr_t numBytes, U32 alignment, U32 sectionID, llvm::StringRef SectionName, bool isReadOnly) override {
				return owner.unitmemorymanager->allocateDataSection(numBytes, alignment, sectionID, SectionName, isReadOnly);
			}
			bool finalizeMemory(std::string* ErrMsg = nullptr) override {
				return owner.unitmemorymanager->finalizeMemory(ErrMsg);
			}
		};

		static std::unique_ptr<llvm::orc::ExecutionSession> createES() {
			auto EPC = llvm::orc::SelfExecutorProcessControl::Create();
			WAVM_ASSERT_THROW(EPC);
			return std::make_unique<llvm::orc::ExecutionSession>(std::move(*EPC));
		}
		std::unique_ptr<llvm::orc::ExecutionSession> ES_ptr = createES();
		llvm::orc::ExecutionSession& ES = *ES_ptr;
		llvm::orc::JITDylib &mainJD = ES.createBareJITDylib("main");
		std::unique_ptr<llvm::orc::RTDyldObjectLinkingLayer> objectLayer;
		std::unique_ptr<llvm::orc::IRCompileLayer> compileLayer;
	};
#else
	// ORCv1 JIT compilation unit (LLVM 7-11)
	struct JITModule
	{
		JITModule() {
			objectLayer = std::make_unique<llvm::orc::LegacyRTDyldObjectLinkingLayer>(ES,[this](llvm::orc::VModuleKey K) {
									return llvm::orc::LegacyRTDyldObjectLinkingLayer::Resources{
										unitmemorymanager, std::make_shared<llvm::orc::NullResolver>()
										};
							  },
							  [](llvm::orc::VModuleKey, const llvm::object::ObjectFile &Obj, const llvm::RuntimeDyld::LoadedObjectInfo &o) {
								  //nothing to do
							  },
							  [this](llvm::orc::VModuleKey, const llvm::object::ObjectFile &Obj, const llvm::RuntimeDyld::LoadedObjectInfo &o) {
									for(auto symbolSizePair : llvm::object::computeSymbolSizes(Obj)) {
										auto symbol = symbolSizePair.first;
										auto name = symbol.getName();
										auto address = symbol.getAddress();
										if(symbol.getType() && symbol.getType().get() == llvm::object::SymbolRef::ST_Function && name && address) {
											Uptr loadedAddress = Uptr(*address);
											auto symbolSection = symbol.getSection();
											if(symbolSection)
												loadedAddress += (Uptr)o.getSectionLoadAddress(*symbolSection.get());
											Uptr functionDefIndex;
											if(getFunctionIndexFromExternalName(name->data(),functionDefIndex))
												function_to_offsets[functionDefIndex] = loadedAddress-(uintptr_t)unitmemorymanager->code->data();
#if PRINT_DISASSEMBLY
											disassembleFunction((U8*)loadedAddress, symbolSizePair.second);
#endif
										} else if(symbol.getType() && symbol.getType().get() == llvm::object::SymbolRef::ST_Data && name && *name == getTableSymbolName()) {
											Uptr loadedAddress = Uptr(*address);
											auto symbolSection = symbol.getSection();
											if(symbolSection)
												loadedAddress += (Uptr)o.getSectionLoadAddress(*symbolSection.get());
											table_offset = loadedAddress-(uintptr_t)unitmemorymanager->code->data();
										}
									}
							  }
							  );
			objectLayer->setProcessAllSections(true);
			compileLayer = std::make_unique<CompileLayer>(*objectLayer,llvm::orc::SimpleCompiler(*targetMachine));
		}

		void compile(llvm::Module* llvmModule);

		std::shared_ptr<UnitMemoryManager> unitmemorymanager = std::make_shared<UnitMemoryManager>();

		std::map<unsigned, uintptr_t> function_to_offsets;
		std::vector<uint8_t> final_pic_code;
		uintptr_t table_offset = 0;

		~JITModule()
		{
		}
	private:
		typedef llvm::orc::LegacyIRCompileLayer<llvm::orc::LegacyRTDyldObjectLinkingLayer, llvm::orc::SimpleCompiler> CompileLayer;

		llvm::orc::ExecutionSession ES;
		std::unique_ptr<llvm::orc::LegacyRTDyldObjectLinkingLayer> objectLayer;
		std::unique_ptr<CompileLayer> compileLayer;
	};
#endif

	static Uptr printedModuleId = 0;

	void printModule(const llvm::Module* llvmModule,const char* filename)
	{
		std::error_code errorCode;
		std::string augmentedFilename = std::string(filename) + std::to_string(printedModuleId++) + ".ll";
#if LLVM_VERSION_MAJOR >= 12
		llvm::raw_fd_ostream dumpFileStream(augmentedFilename,errorCode,llvm::sys::fs::OF_Text);
#else
		llvm::raw_fd_ostream dumpFileStream(augmentedFilename,errorCode,llvm::sys::fs::OpenFlags::F_Text);
#endif
		llvmModule->print(dumpFileStream,nullptr);
		///Log::printf(Log::Category::debug,"Dumped LLVM module to: %s\n",augmentedFilename.c_str());
	}

	void JITModule::compile(llvm::Module* llvmModule)
	{
		// Get a target machine object for this host, and set the module to use its data layout.
		llvmModule->setDataLayout(targetMachine->createDataLayout());

		// Verify the module.
		if(DUMP_UNOPTIMIZED_MODULE) { printModule(llvmModule,"llvmDump"); }
		if(VERIFY_MODULE)
		{
			std::string verifyOutputString;
			llvm::raw_string_ostream verifyOutputStream(verifyOutputString);
			if(llvm::verifyModule(*llvmModule,&verifyOutputStream))
			{
				std::ofstream("/tmp/oc_compile_error.log", std::ios::app)
					<< "LLVM verification errors:\n" << verifyOutputString << std::endl;
				Errors::fatalf("LLVM verification errors:\n%s\n",verifyOutputString.c_str());
			}
			///Log::printf(Log::Category::debug,"Verified LLVM module\n");
		}

		auto fpm = new llvm::legacy::FunctionPassManager(llvmModule);
		fpm->add(llvm::createPromoteMemoryToRegisterPass());
		fpm->add(llvm::createInstructionCombiningPass());
		fpm->add(llvm::createCFGSimplificationPass());
		fpm->add(llvm::createJumpThreadingPass());
#if LLVM_VERSION_MAJOR >= 12
		fpm->add(llvm::createSCCPPass());
#else
		fpm->add(llvm::createConstantPropagationPass());
#endif
		fpm->doInitialization();

		for(auto functionIt = llvmModule->begin();functionIt != llvmModule->end();++functionIt)
		{ fpm->run(*functionIt); }
		delete fpm;

		if(DUMP_OPTIMIZED_MODULE) { printModule(llvmModule,"llvmOptimizedDump"); }

#if LLVM_VERSION_MAJOR >= 12
		std::ofstream("/tmp/oc_compile_error.log", std::ios::app) << "  compile: verify+optimize done, entering ORCv2 codegen..." << std::endl;
		auto ctx = std::make_unique<llvm::LLVMContext>();
		// The module was created with a different context; we need to transfer ownership.
		// We wrap the raw module pointer in a ThreadSafeModule using its existing context.
		llvm::orc::ThreadSafeContext TSCtx(std::unique_ptr<llvm::LLVMContext>(&llvmModule->getContext()));
		std::unique_ptr<llvm::Module> mod(llvmModule);
		llvm::orc::ThreadSafeModule TSM(std::move(mod), std::move(TSCtx));
		std::ofstream("/tmp/oc_compile_error.log", std::ios::app) << "  compile: calling compileLayer->add..." << std::endl;
		auto err = compileLayer->add(mainJD, std::move(TSM));
		if(err) {
			std::string errStr;
			llvm::raw_string_ostream errOS(errStr);
			errOS << err;
			std::ofstream("/tmp/oc_compile_error.log", std::ios::app) << "  compile: compileLayer->add FAILED: " << errStr << std::endl;
		}
		WAVM_ASSERT_THROW(!err);

		std::ofstream("/tmp/oc_compile_error.log", std::ios::app) << "  compile: calling ES.lookup to force materialization..." << std::endl;
		// Force materialization of all symbols
		auto sym = ES.lookup({&mainJD}, ES.intern("__force_materialization__"));
		// We expect the lookup to fail (symbol doesn't exist), but by this point
		// all other symbols will have been materialized. Consume the error.
		if(!sym) {
			std::string errStr;
			llvm::raw_string_ostream errOS(errStr);
			errOS << sym.takeError();
			std::ofstream("/tmp/oc_compile_error.log", std::ios::app) << "  compile: lookup result (expected fail): " << errStr << std::endl;
		}
		std::ofstream("/tmp/oc_compile_error.log", std::ios::app) << "  compile: ORCv2 codegen complete, code ptr="
			<< (void*)unitmemorymanager->code.get()
			<< " code size=" << (unitmemorymanager->code ? unitmemorymanager->code->size() : 0)
			<< " function_to_offsets count=" << function_to_offsets.size() << std::endl;
#else
		llvm::orc::VModuleKey K = ES.allocateVModule();
		std::unique_ptr<llvm::Module> mod(llvmModule);
		WAVM_ASSERT_THROW(!compileLayer->addModule(K, std::move(mod)));
		WAVM_ASSERT_THROW(!compileLayer->emitAndFinalize(K));
#endif

		final_pic_code = std::move(*unitmemorymanager->code);
	}

	instantiated_code instantiateModule(const IR::Module& module, uint64_t stack_size_limit, size_t generated_code_size_limit)
	{
		static bool inited;
		if(!inited) {
			inited = true;
			llvm::InitializeNativeTarget();
			llvm::InitializeNativeTargetAsmPrinter();
			llvm::InitializeNativeTargetAsmParser();
			llvm::InitializeNativeTargetDisassembler();
			llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);

			auto targetTriple = llvm::sys::getProcessTriple();

			llvm::TargetOptions to;
			to.EmitStackSizeSection = 1;

			llvm::SmallVector<std::string,0> targetFeatures;
#if defined(__aarch64__)
			// Tell LLVM's AArch64 code generator not to use X28 — it's our
			// dedicated WASM memory base register (equivalent of GS on x86_64).
			targetFeatures.push_back("+reserve-x28");
#endif
			targetMachine = llvm::EngineBuilder().setRelocationModel(llvm::Reloc::PIC_).setCodeModel(llvm::CodeModel::Small).setTargetOptions(to).selectTarget(
				llvm::Triple(targetTriple),"","",targetFeatures
				);
		}

		// Emit LLVM IR for the module.
		std::ofstream("/tmp/oc_compile_error.log", std::ios::app) << "  instantiateModule: calling emitModule..." << std::endl;
		auto llvmModule = emitModule(module);
		std::ofstream("/tmp/oc_compile_error.log", std::ios::app) << "  instantiateModule: emitModule done, calling compile..." << std::endl;

		// Construct the JIT compilation pipeline for this module.
		auto jitModule = new JITModule();
		// Compile the module.
		jitModule->compile(llvmModule);
		std::ofstream("/tmp/oc_compile_error.log", std::ios::app) << "  instantiateModule: compile done, code size=" << jitModule->final_pic_code.size() << std::endl;

		unsigned num_functions_stack_size_found = 0;
		for(const auto& stacksizes : jitModule->unitmemorymanager->stack_sizes) {
#if LLVM_VERSION_MAJOR < 10
			using de_offset_t = uint32_t;
#else
			using de_offset_t = uint64_t;
#endif
			llvm::DataExtractor ds(llvm::StringRef(reinterpret_cast<const char*>(stacksizes.data()), stacksizes.size()), true, 8);
			de_offset_t offset = 0;

			while(ds.isValidOffsetForAddress(offset)) {
				ds.getAddress(&offset);
				const de_offset_t offset_before_read = offset;
				const uint64_t stack_size = ds.getULEB128(&offset);
				WAVM_ASSERT_THROW(offset_before_read != offset);

				++num_functions_stack_size_found;
				if(stack_size > stack_size_limit) {
					std::ofstream("/tmp/oc_compile_error.log", std::ios::app)
						<< "OC compile _exit(1): stack_size " << stack_size << " > limit " << stack_size_limit << std::endl;
					_exit(1);
				}
			}
		}
		if(num_functions_stack_size_found != module.functions.defs.size()) {
			std::ofstream("/tmp/oc_compile_error.log", std::ios::app)
				<< "OC compile _exit(1): stack_size_found " << num_functions_stack_size_found
				<< " != functions.defs.size " << module.functions.defs.size() << std::endl;
			_exit(1);
		}
		if(jitModule->final_pic_code.size() >= generated_code_size_limit) {
			std::ofstream("/tmp/oc_compile_error.log", std::ios::app)
				<< "OC compile _exit(1): code size " << jitModule->final_pic_code.size()
				<< " >= limit " << generated_code_size_limit << std::endl;
			_exit(1);
		}

		instantiated_code ret;
		ret.code = jitModule->final_pic_code;
		ret.function_offsets = jitModule->function_to_offsets;
		ret.table_offset = jitModule->table_offset;
		return ret;
	}
}
}}}
