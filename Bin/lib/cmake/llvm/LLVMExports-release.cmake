#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "LLVMDemangle" for configuration "Release"
set_property(TARGET LLVMDemangle APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMDemangle PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMDemangle.a"
  )

list(APPEND _cmake_import_check_targets LLVMDemangle )
list(APPEND _cmake_import_check_files_for_LLVMDemangle "${_IMPORT_PREFIX}/lib/libLLVMDemangle.a" )

# Import target "LLVMSupport" for configuration "Release"
set_property(TARGET LLVMSupport APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMSupport PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "ASM;C;CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMSupport.a"
  )

list(APPEND _cmake_import_check_targets LLVMSupport )
list(APPEND _cmake_import_check_files_for_LLVMSupport "${_IMPORT_PREFIX}/lib/libLLVMSupport.a" )

# Import target "LLVMTableGen" for configuration "Release"
set_property(TARGET LLVMTableGen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMTableGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMTableGen.a"
  )

list(APPEND _cmake_import_check_targets LLVMTableGen )
list(APPEND _cmake_import_check_files_for_LLVMTableGen "${_IMPORT_PREFIX}/lib/libLLVMTableGen.a" )

# Import target "LLVMTableGenBasic" for configuration "Release"
set_property(TARGET LLVMTableGenBasic APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMTableGenBasic PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMTableGenBasic.a"
  )

list(APPEND _cmake_import_check_targets LLVMTableGenBasic )
list(APPEND _cmake_import_check_files_for_LLVMTableGenBasic "${_IMPORT_PREFIX}/lib/libLLVMTableGenBasic.a" )

# Import target "LLVMTableGenCommon" for configuration "Release"
set_property(TARGET LLVMTableGenCommon APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMTableGenCommon PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMTableGenCommon.a"
  )

list(APPEND _cmake_import_check_targets LLVMTableGenCommon )
list(APPEND _cmake_import_check_files_for_LLVMTableGenCommon "${_IMPORT_PREFIX}/lib/libLLVMTableGenCommon.a" )

# Import target "llvm-tblgen" for configuration "Release"
set_property(TARGET llvm-tblgen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-tblgen PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-tblgen.exe"
  )

list(APPEND _cmake_import_check_targets llvm-tblgen )
list(APPEND _cmake_import_check_files_for_llvm-tblgen "${_IMPORT_PREFIX}/bin/llvm-tblgen.exe" )

# Import target "LLVMCore" for configuration "Release"
set_property(TARGET LLVMCore APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMCore PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMCore.a"
  )

list(APPEND _cmake_import_check_targets LLVMCore )
list(APPEND _cmake_import_check_files_for_LLVMCore "${_IMPORT_PREFIX}/lib/libLLVMCore.a" )

# Import target "LLVMFuzzerCLI" for configuration "Release"
set_property(TARGET LLVMFuzzerCLI APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMFuzzerCLI PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMFuzzerCLI.a"
  )

list(APPEND _cmake_import_check_targets LLVMFuzzerCLI )
list(APPEND _cmake_import_check_files_for_LLVMFuzzerCLI "${_IMPORT_PREFIX}/lib/libLLVMFuzzerCLI.a" )

# Import target "LLVMFuzzMutate" for configuration "Release"
set_property(TARGET LLVMFuzzMutate APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMFuzzMutate PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMFuzzMutate.a"
  )

list(APPEND _cmake_import_check_targets LLVMFuzzMutate )
list(APPEND _cmake_import_check_files_for_LLVMFuzzMutate "${_IMPORT_PREFIX}/lib/libLLVMFuzzMutate.a" )

# Import target "LLVMFileCheck" for configuration "Release"
set_property(TARGET LLVMFileCheck APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMFileCheck PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMFileCheck.a"
  )

list(APPEND _cmake_import_check_targets LLVMFileCheck )
list(APPEND _cmake_import_check_files_for_LLVMFileCheck "${_IMPORT_PREFIX}/lib/libLLVMFileCheck.a" )

# Import target "LLVMInterfaceStub" for configuration "Release"
set_property(TARGET LLVMInterfaceStub APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMInterfaceStub PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMInterfaceStub.a"
  )

list(APPEND _cmake_import_check_targets LLVMInterfaceStub )
list(APPEND _cmake_import_check_files_for_LLVMInterfaceStub "${_IMPORT_PREFIX}/lib/libLLVMInterfaceStub.a" )

# Import target "LLVMIRPrinter" for configuration "Release"
set_property(TARGET LLVMIRPrinter APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMIRPrinter PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMIRPrinter.a"
  )

list(APPEND _cmake_import_check_targets LLVMIRPrinter )
list(APPEND _cmake_import_check_files_for_LLVMIRPrinter "${_IMPORT_PREFIX}/lib/libLLVMIRPrinter.a" )

# Import target "LLVMIRReader" for configuration "Release"
set_property(TARGET LLVMIRReader APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMIRReader PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMIRReader.a"
  )

list(APPEND _cmake_import_check_targets LLVMIRReader )
list(APPEND _cmake_import_check_files_for_LLVMIRReader "${_IMPORT_PREFIX}/lib/libLLVMIRReader.a" )

# Import target "LLVMCGData" for configuration "Release"
set_property(TARGET LLVMCGData APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMCGData PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMCGData.a"
  )

list(APPEND _cmake_import_check_targets LLVMCGData )
list(APPEND _cmake_import_check_files_for_LLVMCGData "${_IMPORT_PREFIX}/lib/libLLVMCGData.a" )

# Import target "LLVMCodeGen" for configuration "Release"
set_property(TARGET LLVMCodeGen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMCodeGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMCodeGen.a"
  )

list(APPEND _cmake_import_check_targets LLVMCodeGen )
list(APPEND _cmake_import_check_files_for_LLVMCodeGen "${_IMPORT_PREFIX}/lib/libLLVMCodeGen.a" )

# Import target "LLVMSelectionDAG" for configuration "Release"
set_property(TARGET LLVMSelectionDAG APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMSelectionDAG PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMSelectionDAG.a"
  )

list(APPEND _cmake_import_check_targets LLVMSelectionDAG )
list(APPEND _cmake_import_check_files_for_LLVMSelectionDAG "${_IMPORT_PREFIX}/lib/libLLVMSelectionDAG.a" )

# Import target "LLVMAsmPrinter" for configuration "Release"
set_property(TARGET LLVMAsmPrinter APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMAsmPrinter PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMAsmPrinter.a"
  )

list(APPEND _cmake_import_check_targets LLVMAsmPrinter )
list(APPEND _cmake_import_check_files_for_LLVMAsmPrinter "${_IMPORT_PREFIX}/lib/libLLVMAsmPrinter.a" )

# Import target "LLVMMIRParser" for configuration "Release"
set_property(TARGET LLVMMIRParser APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMMIRParser PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMMIRParser.a"
  )

list(APPEND _cmake_import_check_targets LLVMMIRParser )
list(APPEND _cmake_import_check_files_for_LLVMMIRParser "${_IMPORT_PREFIX}/lib/libLLVMMIRParser.a" )

# Import target "LLVMGlobalISel" for configuration "Release"
set_property(TARGET LLVMGlobalISel APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMGlobalISel PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMGlobalISel.a"
  )

list(APPEND _cmake_import_check_targets LLVMGlobalISel )
list(APPEND _cmake_import_check_files_for_LLVMGlobalISel "${_IMPORT_PREFIX}/lib/libLLVMGlobalISel.a" )

# Import target "LLVMCodeGenTypes" for configuration "Release"
set_property(TARGET LLVMCodeGenTypes APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMCodeGenTypes PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMCodeGenTypes.a"
  )

list(APPEND _cmake_import_check_targets LLVMCodeGenTypes )
list(APPEND _cmake_import_check_files_for_LLVMCodeGenTypes "${_IMPORT_PREFIX}/lib/libLLVMCodeGenTypes.a" )

# Import target "LLVMBinaryFormat" for configuration "Release"
set_property(TARGET LLVMBinaryFormat APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMBinaryFormat PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMBinaryFormat.a"
  )

list(APPEND _cmake_import_check_targets LLVMBinaryFormat )
list(APPEND _cmake_import_check_files_for_LLVMBinaryFormat "${_IMPORT_PREFIX}/lib/libLLVMBinaryFormat.a" )

# Import target "LLVMBitReader" for configuration "Release"
set_property(TARGET LLVMBitReader APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMBitReader PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMBitReader.a"
  )

list(APPEND _cmake_import_check_targets LLVMBitReader )
list(APPEND _cmake_import_check_files_for_LLVMBitReader "${_IMPORT_PREFIX}/lib/libLLVMBitReader.a" )

# Import target "LLVMBitWriter" for configuration "Release"
set_property(TARGET LLVMBitWriter APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMBitWriter PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMBitWriter.a"
  )

list(APPEND _cmake_import_check_targets LLVMBitWriter )
list(APPEND _cmake_import_check_files_for_LLVMBitWriter "${_IMPORT_PREFIX}/lib/libLLVMBitWriter.a" )

# Import target "LLVMBitstreamReader" for configuration "Release"
set_property(TARGET LLVMBitstreamReader APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMBitstreamReader PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMBitstreamReader.a"
  )

list(APPEND _cmake_import_check_targets LLVMBitstreamReader )
list(APPEND _cmake_import_check_files_for_LLVMBitstreamReader "${_IMPORT_PREFIX}/lib/libLLVMBitstreamReader.a" )

# Import target "LLVMDWARFLinker" for configuration "Release"
set_property(TARGET LLVMDWARFLinker APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMDWARFLinker PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMDWARFLinker.a"
  )

list(APPEND _cmake_import_check_targets LLVMDWARFLinker )
list(APPEND _cmake_import_check_files_for_LLVMDWARFLinker "${_IMPORT_PREFIX}/lib/libLLVMDWARFLinker.a" )

# Import target "LLVMDWARFLinkerClassic" for configuration "Release"
set_property(TARGET LLVMDWARFLinkerClassic APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMDWARFLinkerClassic PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMDWARFLinkerClassic.a"
  )

list(APPEND _cmake_import_check_targets LLVMDWARFLinkerClassic )
list(APPEND _cmake_import_check_files_for_LLVMDWARFLinkerClassic "${_IMPORT_PREFIX}/lib/libLLVMDWARFLinkerClassic.a" )

# Import target "LLVMDWARFLinkerParallel" for configuration "Release"
set_property(TARGET LLVMDWARFLinkerParallel APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMDWARFLinkerParallel PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMDWARFLinkerParallel.a"
  )

list(APPEND _cmake_import_check_targets LLVMDWARFLinkerParallel )
list(APPEND _cmake_import_check_files_for_LLVMDWARFLinkerParallel "${_IMPORT_PREFIX}/lib/libLLVMDWARFLinkerParallel.a" )

# Import target "LLVMExtensions" for configuration "Release"
set_property(TARGET LLVMExtensions APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMExtensions PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMExtensions.a"
  )

list(APPEND _cmake_import_check_targets LLVMExtensions )
list(APPEND _cmake_import_check_files_for_LLVMExtensions "${_IMPORT_PREFIX}/lib/libLLVMExtensions.a" )

# Import target "LLVMFrontendAtomic" for configuration "Release"
set_property(TARGET LLVMFrontendAtomic APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMFrontendAtomic PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMFrontendAtomic.a"
  )

list(APPEND _cmake_import_check_targets LLVMFrontendAtomic )
list(APPEND _cmake_import_check_files_for_LLVMFrontendAtomic "${_IMPORT_PREFIX}/lib/libLLVMFrontendAtomic.a" )

# Import target "LLVMFrontendDriver" for configuration "Release"
set_property(TARGET LLVMFrontendDriver APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMFrontendDriver PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMFrontendDriver.a"
  )

list(APPEND _cmake_import_check_targets LLVMFrontendDriver )
list(APPEND _cmake_import_check_files_for_LLVMFrontendDriver "${_IMPORT_PREFIX}/lib/libLLVMFrontendDriver.a" )

# Import target "LLVMFrontendHLSL" for configuration "Release"
set_property(TARGET LLVMFrontendHLSL APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMFrontendHLSL PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMFrontendHLSL.a"
  )

list(APPEND _cmake_import_check_targets LLVMFrontendHLSL )
list(APPEND _cmake_import_check_files_for_LLVMFrontendHLSL "${_IMPORT_PREFIX}/lib/libLLVMFrontendHLSL.a" )

# Import target "LLVMFrontendOpenACC" for configuration "Release"
set_property(TARGET LLVMFrontendOpenACC APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMFrontendOpenACC PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMFrontendOpenACC.a"
  )

list(APPEND _cmake_import_check_targets LLVMFrontendOpenACC )
list(APPEND _cmake_import_check_files_for_LLVMFrontendOpenACC "${_IMPORT_PREFIX}/lib/libLLVMFrontendOpenACC.a" )

# Import target "LLVMFrontendOpenMP" for configuration "Release"
set_property(TARGET LLVMFrontendOpenMP APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMFrontendOpenMP PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMFrontendOpenMP.a"
  )

list(APPEND _cmake_import_check_targets LLVMFrontendOpenMP )
list(APPEND _cmake_import_check_files_for_LLVMFrontendOpenMP "${_IMPORT_PREFIX}/lib/libLLVMFrontendOpenMP.a" )

# Import target "LLVMFrontendOffloading" for configuration "Release"
set_property(TARGET LLVMFrontendOffloading APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMFrontendOffloading PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMFrontendOffloading.a"
  )

list(APPEND _cmake_import_check_targets LLVMFrontendOffloading )
list(APPEND _cmake_import_check_files_for_LLVMFrontendOffloading "${_IMPORT_PREFIX}/lib/libLLVMFrontendOffloading.a" )

# Import target "LLVMTransformUtils" for configuration "Release"
set_property(TARGET LLVMTransformUtils APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMTransformUtils PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMTransformUtils.a"
  )

list(APPEND _cmake_import_check_targets LLVMTransformUtils )
list(APPEND _cmake_import_check_files_for_LLVMTransformUtils "${_IMPORT_PREFIX}/lib/libLLVMTransformUtils.a" )

# Import target "LLVMInstrumentation" for configuration "Release"
set_property(TARGET LLVMInstrumentation APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMInstrumentation PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMInstrumentation.a"
  )

list(APPEND _cmake_import_check_targets LLVMInstrumentation )
list(APPEND _cmake_import_check_files_for_LLVMInstrumentation "${_IMPORT_PREFIX}/lib/libLLVMInstrumentation.a" )

# Import target "LLVMAggressiveInstCombine" for configuration "Release"
set_property(TARGET LLVMAggressiveInstCombine APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMAggressiveInstCombine PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMAggressiveInstCombine.a"
  )

list(APPEND _cmake_import_check_targets LLVMAggressiveInstCombine )
list(APPEND _cmake_import_check_files_for_LLVMAggressiveInstCombine "${_IMPORT_PREFIX}/lib/libLLVMAggressiveInstCombine.a" )

# Import target "LLVMInstCombine" for configuration "Release"
set_property(TARGET LLVMInstCombine APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMInstCombine PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMInstCombine.a"
  )

list(APPEND _cmake_import_check_targets LLVMInstCombine )
list(APPEND _cmake_import_check_files_for_LLVMInstCombine "${_IMPORT_PREFIX}/lib/libLLVMInstCombine.a" )

# Import target "LLVMScalarOpts" for configuration "Release"
set_property(TARGET LLVMScalarOpts APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMScalarOpts PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMScalarOpts.a"
  )

list(APPEND _cmake_import_check_targets LLVMScalarOpts )
list(APPEND _cmake_import_check_files_for_LLVMScalarOpts "${_IMPORT_PREFIX}/lib/libLLVMScalarOpts.a" )

# Import target "LLVMipo" for configuration "Release"
set_property(TARGET LLVMipo APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMipo PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMipo.a"
  )

list(APPEND _cmake_import_check_targets LLVMipo )
list(APPEND _cmake_import_check_files_for_LLVMipo "${_IMPORT_PREFIX}/lib/libLLVMipo.a" )

# Import target "LLVMVectorize" for configuration "Release"
set_property(TARGET LLVMVectorize APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMVectorize PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMVectorize.a"
  )

list(APPEND _cmake_import_check_targets LLVMVectorize )
list(APPEND _cmake_import_check_files_for_LLVMVectorize "${_IMPORT_PREFIX}/lib/libLLVMVectorize.a" )

# Import target "LLVMObjCARCOpts" for configuration "Release"
set_property(TARGET LLVMObjCARCOpts APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMObjCARCOpts PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMObjCARCOpts.a"
  )

list(APPEND _cmake_import_check_targets LLVMObjCARCOpts )
list(APPEND _cmake_import_check_files_for_LLVMObjCARCOpts "${_IMPORT_PREFIX}/lib/libLLVMObjCARCOpts.a" )

# Import target "LLVMCoroutines" for configuration "Release"
set_property(TARGET LLVMCoroutines APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMCoroutines PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMCoroutines.a"
  )

list(APPEND _cmake_import_check_targets LLVMCoroutines )
list(APPEND _cmake_import_check_files_for_LLVMCoroutines "${_IMPORT_PREFIX}/lib/libLLVMCoroutines.a" )

# Import target "LLVMCFGuard" for configuration "Release"
set_property(TARGET LLVMCFGuard APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMCFGuard PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMCFGuard.a"
  )

list(APPEND _cmake_import_check_targets LLVMCFGuard )
list(APPEND _cmake_import_check_files_for_LLVMCFGuard "${_IMPORT_PREFIX}/lib/libLLVMCFGuard.a" )

# Import target "LLVMHipStdPar" for configuration "Release"
set_property(TARGET LLVMHipStdPar APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMHipStdPar PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMHipStdPar.a"
  )

list(APPEND _cmake_import_check_targets LLVMHipStdPar )
list(APPEND _cmake_import_check_files_for_LLVMHipStdPar "${_IMPORT_PREFIX}/lib/libLLVMHipStdPar.a" )

# Import target "LLVMLinker" for configuration "Release"
set_property(TARGET LLVMLinker APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMLinker PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMLinker.a"
  )

list(APPEND _cmake_import_check_targets LLVMLinker )
list(APPEND _cmake_import_check_files_for_LLVMLinker "${_IMPORT_PREFIX}/lib/libLLVMLinker.a" )

# Import target "LLVMAnalysis" for configuration "Release"
set_property(TARGET LLVMAnalysis APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMAnalysis PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMAnalysis.a"
  )

list(APPEND _cmake_import_check_targets LLVMAnalysis )
list(APPEND _cmake_import_check_files_for_LLVMAnalysis "${_IMPORT_PREFIX}/lib/libLLVMAnalysis.a" )

# Import target "LLVMLTO" for configuration "Release"
set_property(TARGET LLVMLTO APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMLTO PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMLTO.a"
  )

list(APPEND _cmake_import_check_targets LLVMLTO )
list(APPEND _cmake_import_check_files_for_LLVMLTO "${_IMPORT_PREFIX}/lib/libLLVMLTO.a" )

# Import target "LLVMMC" for configuration "Release"
set_property(TARGET LLVMMC APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMMC PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMMC.a"
  )

list(APPEND _cmake_import_check_targets LLVMMC )
list(APPEND _cmake_import_check_files_for_LLVMMC "${_IMPORT_PREFIX}/lib/libLLVMMC.a" )

# Import target "LLVMMCParser" for configuration "Release"
set_property(TARGET LLVMMCParser APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMMCParser PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMMCParser.a"
  )

list(APPEND _cmake_import_check_targets LLVMMCParser )
list(APPEND _cmake_import_check_files_for_LLVMMCParser "${_IMPORT_PREFIX}/lib/libLLVMMCParser.a" )

# Import target "LLVMMCDisassembler" for configuration "Release"
set_property(TARGET LLVMMCDisassembler APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMMCDisassembler PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMMCDisassembler.a"
  )

list(APPEND _cmake_import_check_targets LLVMMCDisassembler )
list(APPEND _cmake_import_check_files_for_LLVMMCDisassembler "${_IMPORT_PREFIX}/lib/libLLVMMCDisassembler.a" )

# Import target "LLVMMCA" for configuration "Release"
set_property(TARGET LLVMMCA APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMMCA PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMMCA.a"
  )

list(APPEND _cmake_import_check_targets LLVMMCA )
list(APPEND _cmake_import_check_files_for_LLVMMCA "${_IMPORT_PREFIX}/lib/libLLVMMCA.a" )

# Import target "LLVMObjCopy" for configuration "Release"
set_property(TARGET LLVMObjCopy APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMObjCopy PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMObjCopy.a"
  )

list(APPEND _cmake_import_check_targets LLVMObjCopy )
list(APPEND _cmake_import_check_files_for_LLVMObjCopy "${_IMPORT_PREFIX}/lib/libLLVMObjCopy.a" )

# Import target "LLVMObject" for configuration "Release"
set_property(TARGET LLVMObject APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMObject PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMObject.a"
  )

list(APPEND _cmake_import_check_targets LLVMObject )
list(APPEND _cmake_import_check_files_for_LLVMObject "${_IMPORT_PREFIX}/lib/libLLVMObject.a" )

# Import target "LLVMObjectYAML" for configuration "Release"
set_property(TARGET LLVMObjectYAML APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMObjectYAML PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMObjectYAML.a"
  )

list(APPEND _cmake_import_check_targets LLVMObjectYAML )
list(APPEND _cmake_import_check_files_for_LLVMObjectYAML "${_IMPORT_PREFIX}/lib/libLLVMObjectYAML.a" )

# Import target "LLVMOption" for configuration "Release"
set_property(TARGET LLVMOption APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMOption PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMOption.a"
  )

list(APPEND _cmake_import_check_targets LLVMOption )
list(APPEND _cmake_import_check_files_for_LLVMOption "${_IMPORT_PREFIX}/lib/libLLVMOption.a" )

# Import target "LLVMRemarks" for configuration "Release"
set_property(TARGET LLVMRemarks APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMRemarks PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMRemarks.a"
  )

list(APPEND _cmake_import_check_targets LLVMRemarks )
list(APPEND _cmake_import_check_files_for_LLVMRemarks "${_IMPORT_PREFIX}/lib/libLLVMRemarks.a" )

# Import target "LLVMDebuginfod" for configuration "Release"
set_property(TARGET LLVMDebuginfod APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMDebuginfod PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMDebuginfod.a"
  )

list(APPEND _cmake_import_check_targets LLVMDebuginfod )
list(APPEND _cmake_import_check_files_for_LLVMDebuginfod "${_IMPORT_PREFIX}/lib/libLLVMDebuginfod.a" )

# Import target "LLVMDebugInfoDWARF" for configuration "Release"
set_property(TARGET LLVMDebugInfoDWARF APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMDebugInfoDWARF PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMDebugInfoDWARF.a"
  )

list(APPEND _cmake_import_check_targets LLVMDebugInfoDWARF )
list(APPEND _cmake_import_check_files_for_LLVMDebugInfoDWARF "${_IMPORT_PREFIX}/lib/libLLVMDebugInfoDWARF.a" )

# Import target "LLVMDebugInfoGSYM" for configuration "Release"
set_property(TARGET LLVMDebugInfoGSYM APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMDebugInfoGSYM PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMDebugInfoGSYM.a"
  )

list(APPEND _cmake_import_check_targets LLVMDebugInfoGSYM )
list(APPEND _cmake_import_check_files_for_LLVMDebugInfoGSYM "${_IMPORT_PREFIX}/lib/libLLVMDebugInfoGSYM.a" )

# Import target "LLVMDebugInfoLogicalView" for configuration "Release"
set_property(TARGET LLVMDebugInfoLogicalView APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMDebugInfoLogicalView PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMDebugInfoLogicalView.a"
  )

list(APPEND _cmake_import_check_targets LLVMDebugInfoLogicalView )
list(APPEND _cmake_import_check_files_for_LLVMDebugInfoLogicalView "${_IMPORT_PREFIX}/lib/libLLVMDebugInfoLogicalView.a" )

# Import target "LLVMDebugInfoMSF" for configuration "Release"
set_property(TARGET LLVMDebugInfoMSF APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMDebugInfoMSF PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMDebugInfoMSF.a"
  )

list(APPEND _cmake_import_check_targets LLVMDebugInfoMSF )
list(APPEND _cmake_import_check_files_for_LLVMDebugInfoMSF "${_IMPORT_PREFIX}/lib/libLLVMDebugInfoMSF.a" )

# Import target "LLVMDebugInfoCodeView" for configuration "Release"
set_property(TARGET LLVMDebugInfoCodeView APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMDebugInfoCodeView PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMDebugInfoCodeView.a"
  )

list(APPEND _cmake_import_check_targets LLVMDebugInfoCodeView )
list(APPEND _cmake_import_check_files_for_LLVMDebugInfoCodeView "${_IMPORT_PREFIX}/lib/libLLVMDebugInfoCodeView.a" )

# Import target "LLVMDebugInfoPDB" for configuration "Release"
set_property(TARGET LLVMDebugInfoPDB APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMDebugInfoPDB PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMDebugInfoPDB.a"
  )

list(APPEND _cmake_import_check_targets LLVMDebugInfoPDB )
list(APPEND _cmake_import_check_files_for_LLVMDebugInfoPDB "${_IMPORT_PREFIX}/lib/libLLVMDebugInfoPDB.a" )

# Import target "LLVMSymbolize" for configuration "Release"
set_property(TARGET LLVMSymbolize APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMSymbolize PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMSymbolize.a"
  )

list(APPEND _cmake_import_check_targets LLVMSymbolize )
list(APPEND _cmake_import_check_files_for_LLVMSymbolize "${_IMPORT_PREFIX}/lib/libLLVMSymbolize.a" )

# Import target "LLVMDebugInfoBTF" for configuration "Release"
set_property(TARGET LLVMDebugInfoBTF APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMDebugInfoBTF PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMDebugInfoBTF.a"
  )

list(APPEND _cmake_import_check_targets LLVMDebugInfoBTF )
list(APPEND _cmake_import_check_files_for_LLVMDebugInfoBTF "${_IMPORT_PREFIX}/lib/libLLVMDebugInfoBTF.a" )

# Import target "LLVMDWP" for configuration "Release"
set_property(TARGET LLVMDWP APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMDWP PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMDWP.a"
  )

list(APPEND _cmake_import_check_targets LLVMDWP )
list(APPEND _cmake_import_check_files_for_LLVMDWP "${_IMPORT_PREFIX}/lib/libLLVMDWP.a" )

# Import target "LLVMExecutionEngine" for configuration "Release"
set_property(TARGET LLVMExecutionEngine APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMExecutionEngine PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMExecutionEngine.a"
  )

list(APPEND _cmake_import_check_targets LLVMExecutionEngine )
list(APPEND _cmake_import_check_files_for_LLVMExecutionEngine "${_IMPORT_PREFIX}/lib/libLLVMExecutionEngine.a" )

# Import target "LLVMInterpreter" for configuration "Release"
set_property(TARGET LLVMInterpreter APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMInterpreter PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMInterpreter.a"
  )

list(APPEND _cmake_import_check_targets LLVMInterpreter )
list(APPEND _cmake_import_check_files_for_LLVMInterpreter "${_IMPORT_PREFIX}/lib/libLLVMInterpreter.a" )

# Import target "LLVMJITLink" for configuration "Release"
set_property(TARGET LLVMJITLink APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMJITLink PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMJITLink.a"
  )

list(APPEND _cmake_import_check_targets LLVMJITLink )
list(APPEND _cmake_import_check_files_for_LLVMJITLink "${_IMPORT_PREFIX}/lib/libLLVMJITLink.a" )

# Import target "LLVMMCJIT" for configuration "Release"
set_property(TARGET LLVMMCJIT APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMMCJIT PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMMCJIT.a"
  )

list(APPEND _cmake_import_check_targets LLVMMCJIT )
list(APPEND _cmake_import_check_files_for_LLVMMCJIT "${_IMPORT_PREFIX}/lib/libLLVMMCJIT.a" )

# Import target "LLVMOrcJIT" for configuration "Release"
set_property(TARGET LLVMOrcJIT APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMOrcJIT PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMOrcJIT.a"
  )

list(APPEND _cmake_import_check_targets LLVMOrcJIT )
list(APPEND _cmake_import_check_files_for_LLVMOrcJIT "${_IMPORT_PREFIX}/lib/libLLVMOrcJIT.a" )

# Import target "LLVMOrcDebugging" for configuration "Release"
set_property(TARGET LLVMOrcDebugging APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMOrcDebugging PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMOrcDebugging.a"
  )

list(APPEND _cmake_import_check_targets LLVMOrcDebugging )
list(APPEND _cmake_import_check_files_for_LLVMOrcDebugging "${_IMPORT_PREFIX}/lib/libLLVMOrcDebugging.a" )

# Import target "LLVMOrcShared" for configuration "Release"
set_property(TARGET LLVMOrcShared APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMOrcShared PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMOrcShared.a"
  )

list(APPEND _cmake_import_check_targets LLVMOrcShared )
list(APPEND _cmake_import_check_files_for_LLVMOrcShared "${_IMPORT_PREFIX}/lib/libLLVMOrcShared.a" )

# Import target "LLVMOrcTargetProcess" for configuration "Release"
set_property(TARGET LLVMOrcTargetProcess APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMOrcTargetProcess PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMOrcTargetProcess.a"
  )

list(APPEND _cmake_import_check_targets LLVMOrcTargetProcess )
list(APPEND _cmake_import_check_files_for_LLVMOrcTargetProcess "${_IMPORT_PREFIX}/lib/libLLVMOrcTargetProcess.a" )

# Import target "LLVMRuntimeDyld" for configuration "Release"
set_property(TARGET LLVMRuntimeDyld APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMRuntimeDyld PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMRuntimeDyld.a"
  )

list(APPEND _cmake_import_check_targets LLVMRuntimeDyld )
list(APPEND _cmake_import_check_files_for_LLVMRuntimeDyld "${_IMPORT_PREFIX}/lib/libLLVMRuntimeDyld.a" )

# Import target "LLVMTarget" for configuration "Release"
set_property(TARGET LLVMTarget APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMTarget PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMTarget.a"
  )

list(APPEND _cmake_import_check_targets LLVMTarget )
list(APPEND _cmake_import_check_files_for_LLVMTarget "${_IMPORT_PREFIX}/lib/libLLVMTarget.a" )

# Import target "LLVMAArch64CodeGen" for configuration "Release"
set_property(TARGET LLVMAArch64CodeGen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMAArch64CodeGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMAArch64CodeGen.a"
  )

list(APPEND _cmake_import_check_targets LLVMAArch64CodeGen )
list(APPEND _cmake_import_check_files_for_LLVMAArch64CodeGen "${_IMPORT_PREFIX}/lib/libLLVMAArch64CodeGen.a" )

# Import target "LLVMAArch64AsmParser" for configuration "Release"
set_property(TARGET LLVMAArch64AsmParser APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMAArch64AsmParser PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMAArch64AsmParser.a"
  )

list(APPEND _cmake_import_check_targets LLVMAArch64AsmParser )
list(APPEND _cmake_import_check_files_for_LLVMAArch64AsmParser "${_IMPORT_PREFIX}/lib/libLLVMAArch64AsmParser.a" )

# Import target "LLVMAArch64Disassembler" for configuration "Release"
set_property(TARGET LLVMAArch64Disassembler APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMAArch64Disassembler PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMAArch64Disassembler.a"
  )

list(APPEND _cmake_import_check_targets LLVMAArch64Disassembler )
list(APPEND _cmake_import_check_files_for_LLVMAArch64Disassembler "${_IMPORT_PREFIX}/lib/libLLVMAArch64Disassembler.a" )

# Import target "LLVMAArch64Desc" for configuration "Release"
set_property(TARGET LLVMAArch64Desc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMAArch64Desc PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMAArch64Desc.a"
  )

list(APPEND _cmake_import_check_targets LLVMAArch64Desc )
list(APPEND _cmake_import_check_files_for_LLVMAArch64Desc "${_IMPORT_PREFIX}/lib/libLLVMAArch64Desc.a" )

# Import target "LLVMAArch64Info" for configuration "Release"
set_property(TARGET LLVMAArch64Info APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMAArch64Info PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMAArch64Info.a"
  )

list(APPEND _cmake_import_check_targets LLVMAArch64Info )
list(APPEND _cmake_import_check_files_for_LLVMAArch64Info "${_IMPORT_PREFIX}/lib/libLLVMAArch64Info.a" )

# Import target "LLVMAArch64Utils" for configuration "Release"
set_property(TARGET LLVMAArch64Utils APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMAArch64Utils PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMAArch64Utils.a"
  )

list(APPEND _cmake_import_check_targets LLVMAArch64Utils )
list(APPEND _cmake_import_check_files_for_LLVMAArch64Utils "${_IMPORT_PREFIX}/lib/libLLVMAArch64Utils.a" )

# Import target "LLVMAMDGPUCodeGen" for configuration "Release"
set_property(TARGET LLVMAMDGPUCodeGen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMAMDGPUCodeGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMAMDGPUCodeGen.a"
  )

list(APPEND _cmake_import_check_targets LLVMAMDGPUCodeGen )
list(APPEND _cmake_import_check_files_for_LLVMAMDGPUCodeGen "${_IMPORT_PREFIX}/lib/libLLVMAMDGPUCodeGen.a" )

# Import target "LLVMAMDGPUAsmParser" for configuration "Release"
set_property(TARGET LLVMAMDGPUAsmParser APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMAMDGPUAsmParser PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMAMDGPUAsmParser.a"
  )

list(APPEND _cmake_import_check_targets LLVMAMDGPUAsmParser )
list(APPEND _cmake_import_check_files_for_LLVMAMDGPUAsmParser "${_IMPORT_PREFIX}/lib/libLLVMAMDGPUAsmParser.a" )

# Import target "LLVMAMDGPUDisassembler" for configuration "Release"
set_property(TARGET LLVMAMDGPUDisassembler APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMAMDGPUDisassembler PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMAMDGPUDisassembler.a"
  )

list(APPEND _cmake_import_check_targets LLVMAMDGPUDisassembler )
list(APPEND _cmake_import_check_files_for_LLVMAMDGPUDisassembler "${_IMPORT_PREFIX}/lib/libLLVMAMDGPUDisassembler.a" )

# Import target "LLVMAMDGPUTargetMCA" for configuration "Release"
set_property(TARGET LLVMAMDGPUTargetMCA APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMAMDGPUTargetMCA PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMAMDGPUTargetMCA.a"
  )

list(APPEND _cmake_import_check_targets LLVMAMDGPUTargetMCA )
list(APPEND _cmake_import_check_files_for_LLVMAMDGPUTargetMCA "${_IMPORT_PREFIX}/lib/libLLVMAMDGPUTargetMCA.a" )

# Import target "LLVMAMDGPUDesc" for configuration "Release"
set_property(TARGET LLVMAMDGPUDesc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMAMDGPUDesc PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMAMDGPUDesc.a"
  )

list(APPEND _cmake_import_check_targets LLVMAMDGPUDesc )
list(APPEND _cmake_import_check_files_for_LLVMAMDGPUDesc "${_IMPORT_PREFIX}/lib/libLLVMAMDGPUDesc.a" )

# Import target "LLVMAMDGPUInfo" for configuration "Release"
set_property(TARGET LLVMAMDGPUInfo APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMAMDGPUInfo PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMAMDGPUInfo.a"
  )

list(APPEND _cmake_import_check_targets LLVMAMDGPUInfo )
list(APPEND _cmake_import_check_files_for_LLVMAMDGPUInfo "${_IMPORT_PREFIX}/lib/libLLVMAMDGPUInfo.a" )

# Import target "LLVMAMDGPUUtils" for configuration "Release"
set_property(TARGET LLVMAMDGPUUtils APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMAMDGPUUtils PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMAMDGPUUtils.a"
  )

list(APPEND _cmake_import_check_targets LLVMAMDGPUUtils )
list(APPEND _cmake_import_check_files_for_LLVMAMDGPUUtils "${_IMPORT_PREFIX}/lib/libLLVMAMDGPUUtils.a" )

# Import target "LLVMARMCodeGen" for configuration "Release"
set_property(TARGET LLVMARMCodeGen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMARMCodeGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMARMCodeGen.a"
  )

list(APPEND _cmake_import_check_targets LLVMARMCodeGen )
list(APPEND _cmake_import_check_files_for_LLVMARMCodeGen "${_IMPORT_PREFIX}/lib/libLLVMARMCodeGen.a" )

# Import target "LLVMARMAsmParser" for configuration "Release"
set_property(TARGET LLVMARMAsmParser APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMARMAsmParser PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMARMAsmParser.a"
  )

list(APPEND _cmake_import_check_targets LLVMARMAsmParser )
list(APPEND _cmake_import_check_files_for_LLVMARMAsmParser "${_IMPORT_PREFIX}/lib/libLLVMARMAsmParser.a" )

# Import target "LLVMARMDisassembler" for configuration "Release"
set_property(TARGET LLVMARMDisassembler APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMARMDisassembler PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMARMDisassembler.a"
  )

list(APPEND _cmake_import_check_targets LLVMARMDisassembler )
list(APPEND _cmake_import_check_files_for_LLVMARMDisassembler "${_IMPORT_PREFIX}/lib/libLLVMARMDisassembler.a" )

# Import target "LLVMARMDesc" for configuration "Release"
set_property(TARGET LLVMARMDesc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMARMDesc PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMARMDesc.a"
  )

list(APPEND _cmake_import_check_targets LLVMARMDesc )
list(APPEND _cmake_import_check_files_for_LLVMARMDesc "${_IMPORT_PREFIX}/lib/libLLVMARMDesc.a" )

# Import target "LLVMARMInfo" for configuration "Release"
set_property(TARGET LLVMARMInfo APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMARMInfo PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMARMInfo.a"
  )

list(APPEND _cmake_import_check_targets LLVMARMInfo )
list(APPEND _cmake_import_check_files_for_LLVMARMInfo "${_IMPORT_PREFIX}/lib/libLLVMARMInfo.a" )

# Import target "LLVMARMUtils" for configuration "Release"
set_property(TARGET LLVMARMUtils APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMARMUtils PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMARMUtils.a"
  )

list(APPEND _cmake_import_check_targets LLVMARMUtils )
list(APPEND _cmake_import_check_files_for_LLVMARMUtils "${_IMPORT_PREFIX}/lib/libLLVMARMUtils.a" )

# Import target "LLVMAVRCodeGen" for configuration "Release"
set_property(TARGET LLVMAVRCodeGen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMAVRCodeGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMAVRCodeGen.a"
  )

list(APPEND _cmake_import_check_targets LLVMAVRCodeGen )
list(APPEND _cmake_import_check_files_for_LLVMAVRCodeGen "${_IMPORT_PREFIX}/lib/libLLVMAVRCodeGen.a" )

# Import target "LLVMAVRAsmParser" for configuration "Release"
set_property(TARGET LLVMAVRAsmParser APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMAVRAsmParser PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMAVRAsmParser.a"
  )

list(APPEND _cmake_import_check_targets LLVMAVRAsmParser )
list(APPEND _cmake_import_check_files_for_LLVMAVRAsmParser "${_IMPORT_PREFIX}/lib/libLLVMAVRAsmParser.a" )

# Import target "LLVMAVRDisassembler" for configuration "Release"
set_property(TARGET LLVMAVRDisassembler APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMAVRDisassembler PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMAVRDisassembler.a"
  )

list(APPEND _cmake_import_check_targets LLVMAVRDisassembler )
list(APPEND _cmake_import_check_files_for_LLVMAVRDisassembler "${_IMPORT_PREFIX}/lib/libLLVMAVRDisassembler.a" )

# Import target "LLVMAVRDesc" for configuration "Release"
set_property(TARGET LLVMAVRDesc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMAVRDesc PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMAVRDesc.a"
  )

list(APPEND _cmake_import_check_targets LLVMAVRDesc )
list(APPEND _cmake_import_check_files_for_LLVMAVRDesc "${_IMPORT_PREFIX}/lib/libLLVMAVRDesc.a" )

# Import target "LLVMAVRInfo" for configuration "Release"
set_property(TARGET LLVMAVRInfo APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMAVRInfo PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMAVRInfo.a"
  )

list(APPEND _cmake_import_check_targets LLVMAVRInfo )
list(APPEND _cmake_import_check_files_for_LLVMAVRInfo "${_IMPORT_PREFIX}/lib/libLLVMAVRInfo.a" )

# Import target "LLVMBPFCodeGen" for configuration "Release"
set_property(TARGET LLVMBPFCodeGen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMBPFCodeGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMBPFCodeGen.a"
  )

list(APPEND _cmake_import_check_targets LLVMBPFCodeGen )
list(APPEND _cmake_import_check_files_for_LLVMBPFCodeGen "${_IMPORT_PREFIX}/lib/libLLVMBPFCodeGen.a" )

# Import target "LLVMBPFAsmParser" for configuration "Release"
set_property(TARGET LLVMBPFAsmParser APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMBPFAsmParser PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMBPFAsmParser.a"
  )

list(APPEND _cmake_import_check_targets LLVMBPFAsmParser )
list(APPEND _cmake_import_check_files_for_LLVMBPFAsmParser "${_IMPORT_PREFIX}/lib/libLLVMBPFAsmParser.a" )

# Import target "LLVMBPFDisassembler" for configuration "Release"
set_property(TARGET LLVMBPFDisassembler APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMBPFDisassembler PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMBPFDisassembler.a"
  )

list(APPEND _cmake_import_check_targets LLVMBPFDisassembler )
list(APPEND _cmake_import_check_files_for_LLVMBPFDisassembler "${_IMPORT_PREFIX}/lib/libLLVMBPFDisassembler.a" )

# Import target "LLVMBPFDesc" for configuration "Release"
set_property(TARGET LLVMBPFDesc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMBPFDesc PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMBPFDesc.a"
  )

list(APPEND _cmake_import_check_targets LLVMBPFDesc )
list(APPEND _cmake_import_check_files_for_LLVMBPFDesc "${_IMPORT_PREFIX}/lib/libLLVMBPFDesc.a" )

# Import target "LLVMBPFInfo" for configuration "Release"
set_property(TARGET LLVMBPFInfo APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMBPFInfo PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMBPFInfo.a"
  )

list(APPEND _cmake_import_check_targets LLVMBPFInfo )
list(APPEND _cmake_import_check_files_for_LLVMBPFInfo "${_IMPORT_PREFIX}/lib/libLLVMBPFInfo.a" )

# Import target "LLVMHexagonCodeGen" for configuration "Release"
set_property(TARGET LLVMHexagonCodeGen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMHexagonCodeGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMHexagonCodeGen.a"
  )

list(APPEND _cmake_import_check_targets LLVMHexagonCodeGen )
list(APPEND _cmake_import_check_files_for_LLVMHexagonCodeGen "${_IMPORT_PREFIX}/lib/libLLVMHexagonCodeGen.a" )

# Import target "LLVMHexagonAsmParser" for configuration "Release"
set_property(TARGET LLVMHexagonAsmParser APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMHexagonAsmParser PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMHexagonAsmParser.a"
  )

list(APPEND _cmake_import_check_targets LLVMHexagonAsmParser )
list(APPEND _cmake_import_check_files_for_LLVMHexagonAsmParser "${_IMPORT_PREFIX}/lib/libLLVMHexagonAsmParser.a" )

# Import target "LLVMHexagonDisassembler" for configuration "Release"
set_property(TARGET LLVMHexagonDisassembler APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMHexagonDisassembler PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMHexagonDisassembler.a"
  )

list(APPEND _cmake_import_check_targets LLVMHexagonDisassembler )
list(APPEND _cmake_import_check_files_for_LLVMHexagonDisassembler "${_IMPORT_PREFIX}/lib/libLLVMHexagonDisassembler.a" )

# Import target "LLVMHexagonDesc" for configuration "Release"
set_property(TARGET LLVMHexagonDesc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMHexagonDesc PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMHexagonDesc.a"
  )

list(APPEND _cmake_import_check_targets LLVMHexagonDesc )
list(APPEND _cmake_import_check_files_for_LLVMHexagonDesc "${_IMPORT_PREFIX}/lib/libLLVMHexagonDesc.a" )

# Import target "LLVMHexagonInfo" for configuration "Release"
set_property(TARGET LLVMHexagonInfo APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMHexagonInfo PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMHexagonInfo.a"
  )

list(APPEND _cmake_import_check_targets LLVMHexagonInfo )
list(APPEND _cmake_import_check_files_for_LLVMHexagonInfo "${_IMPORT_PREFIX}/lib/libLLVMHexagonInfo.a" )

# Import target "LLVMLanaiCodeGen" for configuration "Release"
set_property(TARGET LLVMLanaiCodeGen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMLanaiCodeGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMLanaiCodeGen.a"
  )

list(APPEND _cmake_import_check_targets LLVMLanaiCodeGen )
list(APPEND _cmake_import_check_files_for_LLVMLanaiCodeGen "${_IMPORT_PREFIX}/lib/libLLVMLanaiCodeGen.a" )

# Import target "LLVMLanaiAsmParser" for configuration "Release"
set_property(TARGET LLVMLanaiAsmParser APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMLanaiAsmParser PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMLanaiAsmParser.a"
  )

list(APPEND _cmake_import_check_targets LLVMLanaiAsmParser )
list(APPEND _cmake_import_check_files_for_LLVMLanaiAsmParser "${_IMPORT_PREFIX}/lib/libLLVMLanaiAsmParser.a" )

# Import target "LLVMLanaiDisassembler" for configuration "Release"
set_property(TARGET LLVMLanaiDisassembler APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMLanaiDisassembler PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMLanaiDisassembler.a"
  )

list(APPEND _cmake_import_check_targets LLVMLanaiDisassembler )
list(APPEND _cmake_import_check_files_for_LLVMLanaiDisassembler "${_IMPORT_PREFIX}/lib/libLLVMLanaiDisassembler.a" )

# Import target "LLVMLanaiDesc" for configuration "Release"
set_property(TARGET LLVMLanaiDesc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMLanaiDesc PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMLanaiDesc.a"
  )

list(APPEND _cmake_import_check_targets LLVMLanaiDesc )
list(APPEND _cmake_import_check_files_for_LLVMLanaiDesc "${_IMPORT_PREFIX}/lib/libLLVMLanaiDesc.a" )

# Import target "LLVMLanaiInfo" for configuration "Release"
set_property(TARGET LLVMLanaiInfo APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMLanaiInfo PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMLanaiInfo.a"
  )

list(APPEND _cmake_import_check_targets LLVMLanaiInfo )
list(APPEND _cmake_import_check_files_for_LLVMLanaiInfo "${_IMPORT_PREFIX}/lib/libLLVMLanaiInfo.a" )

# Import target "LLVMLoongArchCodeGen" for configuration "Release"
set_property(TARGET LLVMLoongArchCodeGen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMLoongArchCodeGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMLoongArchCodeGen.a"
  )

list(APPEND _cmake_import_check_targets LLVMLoongArchCodeGen )
list(APPEND _cmake_import_check_files_for_LLVMLoongArchCodeGen "${_IMPORT_PREFIX}/lib/libLLVMLoongArchCodeGen.a" )

# Import target "LLVMLoongArchAsmParser" for configuration "Release"
set_property(TARGET LLVMLoongArchAsmParser APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMLoongArchAsmParser PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMLoongArchAsmParser.a"
  )

list(APPEND _cmake_import_check_targets LLVMLoongArchAsmParser )
list(APPEND _cmake_import_check_files_for_LLVMLoongArchAsmParser "${_IMPORT_PREFIX}/lib/libLLVMLoongArchAsmParser.a" )

# Import target "LLVMLoongArchDisassembler" for configuration "Release"
set_property(TARGET LLVMLoongArchDisassembler APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMLoongArchDisassembler PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMLoongArchDisassembler.a"
  )

list(APPEND _cmake_import_check_targets LLVMLoongArchDisassembler )
list(APPEND _cmake_import_check_files_for_LLVMLoongArchDisassembler "${_IMPORT_PREFIX}/lib/libLLVMLoongArchDisassembler.a" )

# Import target "LLVMLoongArchDesc" for configuration "Release"
set_property(TARGET LLVMLoongArchDesc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMLoongArchDesc PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMLoongArchDesc.a"
  )

list(APPEND _cmake_import_check_targets LLVMLoongArchDesc )
list(APPEND _cmake_import_check_files_for_LLVMLoongArchDesc "${_IMPORT_PREFIX}/lib/libLLVMLoongArchDesc.a" )

# Import target "LLVMLoongArchInfo" for configuration "Release"
set_property(TARGET LLVMLoongArchInfo APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMLoongArchInfo PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMLoongArchInfo.a"
  )

list(APPEND _cmake_import_check_targets LLVMLoongArchInfo )
list(APPEND _cmake_import_check_files_for_LLVMLoongArchInfo "${_IMPORT_PREFIX}/lib/libLLVMLoongArchInfo.a" )

# Import target "LLVMMipsCodeGen" for configuration "Release"
set_property(TARGET LLVMMipsCodeGen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMMipsCodeGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMMipsCodeGen.a"
  )

list(APPEND _cmake_import_check_targets LLVMMipsCodeGen )
list(APPEND _cmake_import_check_files_for_LLVMMipsCodeGen "${_IMPORT_PREFIX}/lib/libLLVMMipsCodeGen.a" )

# Import target "LLVMMipsAsmParser" for configuration "Release"
set_property(TARGET LLVMMipsAsmParser APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMMipsAsmParser PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMMipsAsmParser.a"
  )

list(APPEND _cmake_import_check_targets LLVMMipsAsmParser )
list(APPEND _cmake_import_check_files_for_LLVMMipsAsmParser "${_IMPORT_PREFIX}/lib/libLLVMMipsAsmParser.a" )

# Import target "LLVMMipsDisassembler" for configuration "Release"
set_property(TARGET LLVMMipsDisassembler APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMMipsDisassembler PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMMipsDisassembler.a"
  )

list(APPEND _cmake_import_check_targets LLVMMipsDisassembler )
list(APPEND _cmake_import_check_files_for_LLVMMipsDisassembler "${_IMPORT_PREFIX}/lib/libLLVMMipsDisassembler.a" )

# Import target "LLVMMipsDesc" for configuration "Release"
set_property(TARGET LLVMMipsDesc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMMipsDesc PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMMipsDesc.a"
  )

list(APPEND _cmake_import_check_targets LLVMMipsDesc )
list(APPEND _cmake_import_check_files_for_LLVMMipsDesc "${_IMPORT_PREFIX}/lib/libLLVMMipsDesc.a" )

# Import target "LLVMMipsInfo" for configuration "Release"
set_property(TARGET LLVMMipsInfo APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMMipsInfo PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMMipsInfo.a"
  )

list(APPEND _cmake_import_check_targets LLVMMipsInfo )
list(APPEND _cmake_import_check_files_for_LLVMMipsInfo "${_IMPORT_PREFIX}/lib/libLLVMMipsInfo.a" )

# Import target "LLVMMSP430CodeGen" for configuration "Release"
set_property(TARGET LLVMMSP430CodeGen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMMSP430CodeGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMMSP430CodeGen.a"
  )

list(APPEND _cmake_import_check_targets LLVMMSP430CodeGen )
list(APPEND _cmake_import_check_files_for_LLVMMSP430CodeGen "${_IMPORT_PREFIX}/lib/libLLVMMSP430CodeGen.a" )

# Import target "LLVMMSP430Desc" for configuration "Release"
set_property(TARGET LLVMMSP430Desc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMMSP430Desc PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMMSP430Desc.a"
  )

list(APPEND _cmake_import_check_targets LLVMMSP430Desc )
list(APPEND _cmake_import_check_files_for_LLVMMSP430Desc "${_IMPORT_PREFIX}/lib/libLLVMMSP430Desc.a" )

# Import target "LLVMMSP430Info" for configuration "Release"
set_property(TARGET LLVMMSP430Info APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMMSP430Info PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMMSP430Info.a"
  )

list(APPEND _cmake_import_check_targets LLVMMSP430Info )
list(APPEND _cmake_import_check_files_for_LLVMMSP430Info "${_IMPORT_PREFIX}/lib/libLLVMMSP430Info.a" )

# Import target "LLVMMSP430AsmParser" for configuration "Release"
set_property(TARGET LLVMMSP430AsmParser APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMMSP430AsmParser PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMMSP430AsmParser.a"
  )

list(APPEND _cmake_import_check_targets LLVMMSP430AsmParser )
list(APPEND _cmake_import_check_files_for_LLVMMSP430AsmParser "${_IMPORT_PREFIX}/lib/libLLVMMSP430AsmParser.a" )

# Import target "LLVMMSP430Disassembler" for configuration "Release"
set_property(TARGET LLVMMSP430Disassembler APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMMSP430Disassembler PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMMSP430Disassembler.a"
  )

list(APPEND _cmake_import_check_targets LLVMMSP430Disassembler )
list(APPEND _cmake_import_check_files_for_LLVMMSP430Disassembler "${_IMPORT_PREFIX}/lib/libLLVMMSP430Disassembler.a" )

# Import target "LLVMNVPTXCodeGen" for configuration "Release"
set_property(TARGET LLVMNVPTXCodeGen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMNVPTXCodeGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMNVPTXCodeGen.a"
  )

list(APPEND _cmake_import_check_targets LLVMNVPTXCodeGen )
list(APPEND _cmake_import_check_files_for_LLVMNVPTXCodeGen "${_IMPORT_PREFIX}/lib/libLLVMNVPTXCodeGen.a" )

# Import target "LLVMNVPTXDesc" for configuration "Release"
set_property(TARGET LLVMNVPTXDesc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMNVPTXDesc PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMNVPTXDesc.a"
  )

list(APPEND _cmake_import_check_targets LLVMNVPTXDesc )
list(APPEND _cmake_import_check_files_for_LLVMNVPTXDesc "${_IMPORT_PREFIX}/lib/libLLVMNVPTXDesc.a" )

# Import target "LLVMNVPTXInfo" for configuration "Release"
set_property(TARGET LLVMNVPTXInfo APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMNVPTXInfo PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMNVPTXInfo.a"
  )

list(APPEND _cmake_import_check_targets LLVMNVPTXInfo )
list(APPEND _cmake_import_check_files_for_LLVMNVPTXInfo "${_IMPORT_PREFIX}/lib/libLLVMNVPTXInfo.a" )

# Import target "LLVMPowerPCCodeGen" for configuration "Release"
set_property(TARGET LLVMPowerPCCodeGen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMPowerPCCodeGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMPowerPCCodeGen.a"
  )

list(APPEND _cmake_import_check_targets LLVMPowerPCCodeGen )
list(APPEND _cmake_import_check_files_for_LLVMPowerPCCodeGen "${_IMPORT_PREFIX}/lib/libLLVMPowerPCCodeGen.a" )

# Import target "LLVMPowerPCAsmParser" for configuration "Release"
set_property(TARGET LLVMPowerPCAsmParser APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMPowerPCAsmParser PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMPowerPCAsmParser.a"
  )

list(APPEND _cmake_import_check_targets LLVMPowerPCAsmParser )
list(APPEND _cmake_import_check_files_for_LLVMPowerPCAsmParser "${_IMPORT_PREFIX}/lib/libLLVMPowerPCAsmParser.a" )

# Import target "LLVMPowerPCDisassembler" for configuration "Release"
set_property(TARGET LLVMPowerPCDisassembler APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMPowerPCDisassembler PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMPowerPCDisassembler.a"
  )

list(APPEND _cmake_import_check_targets LLVMPowerPCDisassembler )
list(APPEND _cmake_import_check_files_for_LLVMPowerPCDisassembler "${_IMPORT_PREFIX}/lib/libLLVMPowerPCDisassembler.a" )

# Import target "LLVMPowerPCDesc" for configuration "Release"
set_property(TARGET LLVMPowerPCDesc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMPowerPCDesc PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMPowerPCDesc.a"
  )

list(APPEND _cmake_import_check_targets LLVMPowerPCDesc )
list(APPEND _cmake_import_check_files_for_LLVMPowerPCDesc "${_IMPORT_PREFIX}/lib/libLLVMPowerPCDesc.a" )

# Import target "LLVMPowerPCInfo" for configuration "Release"
set_property(TARGET LLVMPowerPCInfo APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMPowerPCInfo PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMPowerPCInfo.a"
  )

list(APPEND _cmake_import_check_targets LLVMPowerPCInfo )
list(APPEND _cmake_import_check_files_for_LLVMPowerPCInfo "${_IMPORT_PREFIX}/lib/libLLVMPowerPCInfo.a" )

# Import target "LLVMRISCVCodeGen" for configuration "Release"
set_property(TARGET LLVMRISCVCodeGen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMRISCVCodeGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMRISCVCodeGen.a"
  )

list(APPEND _cmake_import_check_targets LLVMRISCVCodeGen )
list(APPEND _cmake_import_check_files_for_LLVMRISCVCodeGen "${_IMPORT_PREFIX}/lib/libLLVMRISCVCodeGen.a" )

# Import target "LLVMRISCVAsmParser" for configuration "Release"
set_property(TARGET LLVMRISCVAsmParser APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMRISCVAsmParser PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMRISCVAsmParser.a"
  )

list(APPEND _cmake_import_check_targets LLVMRISCVAsmParser )
list(APPEND _cmake_import_check_files_for_LLVMRISCVAsmParser "${_IMPORT_PREFIX}/lib/libLLVMRISCVAsmParser.a" )

# Import target "LLVMRISCVDisassembler" for configuration "Release"
set_property(TARGET LLVMRISCVDisassembler APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMRISCVDisassembler PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMRISCVDisassembler.a"
  )

list(APPEND _cmake_import_check_targets LLVMRISCVDisassembler )
list(APPEND _cmake_import_check_files_for_LLVMRISCVDisassembler "${_IMPORT_PREFIX}/lib/libLLVMRISCVDisassembler.a" )

# Import target "LLVMRISCVDesc" for configuration "Release"
set_property(TARGET LLVMRISCVDesc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMRISCVDesc PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMRISCVDesc.a"
  )

list(APPEND _cmake_import_check_targets LLVMRISCVDesc )
list(APPEND _cmake_import_check_files_for_LLVMRISCVDesc "${_IMPORT_PREFIX}/lib/libLLVMRISCVDesc.a" )

# Import target "LLVMRISCVTargetMCA" for configuration "Release"
set_property(TARGET LLVMRISCVTargetMCA APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMRISCVTargetMCA PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMRISCVTargetMCA.a"
  )

list(APPEND _cmake_import_check_targets LLVMRISCVTargetMCA )
list(APPEND _cmake_import_check_files_for_LLVMRISCVTargetMCA "${_IMPORT_PREFIX}/lib/libLLVMRISCVTargetMCA.a" )

# Import target "LLVMRISCVInfo" for configuration "Release"
set_property(TARGET LLVMRISCVInfo APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMRISCVInfo PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMRISCVInfo.a"
  )

list(APPEND _cmake_import_check_targets LLVMRISCVInfo )
list(APPEND _cmake_import_check_files_for_LLVMRISCVInfo "${_IMPORT_PREFIX}/lib/libLLVMRISCVInfo.a" )

# Import target "LLVMSparcCodeGen" for configuration "Release"
set_property(TARGET LLVMSparcCodeGen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMSparcCodeGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMSparcCodeGen.a"
  )

list(APPEND _cmake_import_check_targets LLVMSparcCodeGen )
list(APPEND _cmake_import_check_files_for_LLVMSparcCodeGen "${_IMPORT_PREFIX}/lib/libLLVMSparcCodeGen.a" )

# Import target "LLVMSparcAsmParser" for configuration "Release"
set_property(TARGET LLVMSparcAsmParser APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMSparcAsmParser PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMSparcAsmParser.a"
  )

list(APPEND _cmake_import_check_targets LLVMSparcAsmParser )
list(APPEND _cmake_import_check_files_for_LLVMSparcAsmParser "${_IMPORT_PREFIX}/lib/libLLVMSparcAsmParser.a" )

# Import target "LLVMSparcDisassembler" for configuration "Release"
set_property(TARGET LLVMSparcDisassembler APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMSparcDisassembler PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMSparcDisassembler.a"
  )

list(APPEND _cmake_import_check_targets LLVMSparcDisassembler )
list(APPEND _cmake_import_check_files_for_LLVMSparcDisassembler "${_IMPORT_PREFIX}/lib/libLLVMSparcDisassembler.a" )

# Import target "LLVMSparcDesc" for configuration "Release"
set_property(TARGET LLVMSparcDesc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMSparcDesc PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMSparcDesc.a"
  )

list(APPEND _cmake_import_check_targets LLVMSparcDesc )
list(APPEND _cmake_import_check_files_for_LLVMSparcDesc "${_IMPORT_PREFIX}/lib/libLLVMSparcDesc.a" )

# Import target "LLVMSparcInfo" for configuration "Release"
set_property(TARGET LLVMSparcInfo APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMSparcInfo PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMSparcInfo.a"
  )

list(APPEND _cmake_import_check_targets LLVMSparcInfo )
list(APPEND _cmake_import_check_files_for_LLVMSparcInfo "${_IMPORT_PREFIX}/lib/libLLVMSparcInfo.a" )

# Import target "LLVMSPIRVCodeGen" for configuration "Release"
set_property(TARGET LLVMSPIRVCodeGen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMSPIRVCodeGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMSPIRVCodeGen.a"
  )

list(APPEND _cmake_import_check_targets LLVMSPIRVCodeGen )
list(APPEND _cmake_import_check_files_for_LLVMSPIRVCodeGen "${_IMPORT_PREFIX}/lib/libLLVMSPIRVCodeGen.a" )

# Import target "LLVMSPIRVDesc" for configuration "Release"
set_property(TARGET LLVMSPIRVDesc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMSPIRVDesc PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMSPIRVDesc.a"
  )

list(APPEND _cmake_import_check_targets LLVMSPIRVDesc )
list(APPEND _cmake_import_check_files_for_LLVMSPIRVDesc "${_IMPORT_PREFIX}/lib/libLLVMSPIRVDesc.a" )

# Import target "LLVMSPIRVInfo" for configuration "Release"
set_property(TARGET LLVMSPIRVInfo APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMSPIRVInfo PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMSPIRVInfo.a"
  )

list(APPEND _cmake_import_check_targets LLVMSPIRVInfo )
list(APPEND _cmake_import_check_files_for_LLVMSPIRVInfo "${_IMPORT_PREFIX}/lib/libLLVMSPIRVInfo.a" )

# Import target "LLVMSPIRVAnalysis" for configuration "Release"
set_property(TARGET LLVMSPIRVAnalysis APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMSPIRVAnalysis PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMSPIRVAnalysis.a"
  )

list(APPEND _cmake_import_check_targets LLVMSPIRVAnalysis )
list(APPEND _cmake_import_check_files_for_LLVMSPIRVAnalysis "${_IMPORT_PREFIX}/lib/libLLVMSPIRVAnalysis.a" )

# Import target "LLVMSystemZCodeGen" for configuration "Release"
set_property(TARGET LLVMSystemZCodeGen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMSystemZCodeGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMSystemZCodeGen.a"
  )

list(APPEND _cmake_import_check_targets LLVMSystemZCodeGen )
list(APPEND _cmake_import_check_files_for_LLVMSystemZCodeGen "${_IMPORT_PREFIX}/lib/libLLVMSystemZCodeGen.a" )

# Import target "LLVMSystemZAsmParser" for configuration "Release"
set_property(TARGET LLVMSystemZAsmParser APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMSystemZAsmParser PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMSystemZAsmParser.a"
  )

list(APPEND _cmake_import_check_targets LLVMSystemZAsmParser )
list(APPEND _cmake_import_check_files_for_LLVMSystemZAsmParser "${_IMPORT_PREFIX}/lib/libLLVMSystemZAsmParser.a" )

# Import target "LLVMSystemZDisassembler" for configuration "Release"
set_property(TARGET LLVMSystemZDisassembler APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMSystemZDisassembler PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMSystemZDisassembler.a"
  )

list(APPEND _cmake_import_check_targets LLVMSystemZDisassembler )
list(APPEND _cmake_import_check_files_for_LLVMSystemZDisassembler "${_IMPORT_PREFIX}/lib/libLLVMSystemZDisassembler.a" )

# Import target "LLVMSystemZDesc" for configuration "Release"
set_property(TARGET LLVMSystemZDesc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMSystemZDesc PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMSystemZDesc.a"
  )

list(APPEND _cmake_import_check_targets LLVMSystemZDesc )
list(APPEND _cmake_import_check_files_for_LLVMSystemZDesc "${_IMPORT_PREFIX}/lib/libLLVMSystemZDesc.a" )

# Import target "LLVMSystemZInfo" for configuration "Release"
set_property(TARGET LLVMSystemZInfo APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMSystemZInfo PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMSystemZInfo.a"
  )

list(APPEND _cmake_import_check_targets LLVMSystemZInfo )
list(APPEND _cmake_import_check_files_for_LLVMSystemZInfo "${_IMPORT_PREFIX}/lib/libLLVMSystemZInfo.a" )

# Import target "LLVMVECodeGen" for configuration "Release"
set_property(TARGET LLVMVECodeGen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMVECodeGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMVECodeGen.a"
  )

list(APPEND _cmake_import_check_targets LLVMVECodeGen )
list(APPEND _cmake_import_check_files_for_LLVMVECodeGen "${_IMPORT_PREFIX}/lib/libLLVMVECodeGen.a" )

# Import target "LLVMVEAsmParser" for configuration "Release"
set_property(TARGET LLVMVEAsmParser APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMVEAsmParser PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMVEAsmParser.a"
  )

list(APPEND _cmake_import_check_targets LLVMVEAsmParser )
list(APPEND _cmake_import_check_files_for_LLVMVEAsmParser "${_IMPORT_PREFIX}/lib/libLLVMVEAsmParser.a" )

# Import target "LLVMVEDisassembler" for configuration "Release"
set_property(TARGET LLVMVEDisassembler APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMVEDisassembler PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMVEDisassembler.a"
  )

list(APPEND _cmake_import_check_targets LLVMVEDisassembler )
list(APPEND _cmake_import_check_files_for_LLVMVEDisassembler "${_IMPORT_PREFIX}/lib/libLLVMVEDisassembler.a" )

# Import target "LLVMVEInfo" for configuration "Release"
set_property(TARGET LLVMVEInfo APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMVEInfo PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMVEInfo.a"
  )

list(APPEND _cmake_import_check_targets LLVMVEInfo )
list(APPEND _cmake_import_check_files_for_LLVMVEInfo "${_IMPORT_PREFIX}/lib/libLLVMVEInfo.a" )

# Import target "LLVMVEDesc" for configuration "Release"
set_property(TARGET LLVMVEDesc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMVEDesc PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMVEDesc.a"
  )

list(APPEND _cmake_import_check_targets LLVMVEDesc )
list(APPEND _cmake_import_check_files_for_LLVMVEDesc "${_IMPORT_PREFIX}/lib/libLLVMVEDesc.a" )

# Import target "LLVMWebAssemblyCodeGen" for configuration "Release"
set_property(TARGET LLVMWebAssemblyCodeGen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMWebAssemblyCodeGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMWebAssemblyCodeGen.a"
  )

list(APPEND _cmake_import_check_targets LLVMWebAssemblyCodeGen )
list(APPEND _cmake_import_check_files_for_LLVMWebAssemblyCodeGen "${_IMPORT_PREFIX}/lib/libLLVMWebAssemblyCodeGen.a" )

# Import target "LLVMWebAssemblyAsmParser" for configuration "Release"
set_property(TARGET LLVMWebAssemblyAsmParser APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMWebAssemblyAsmParser PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMWebAssemblyAsmParser.a"
  )

list(APPEND _cmake_import_check_targets LLVMWebAssemblyAsmParser )
list(APPEND _cmake_import_check_files_for_LLVMWebAssemblyAsmParser "${_IMPORT_PREFIX}/lib/libLLVMWebAssemblyAsmParser.a" )

# Import target "LLVMWebAssemblyDisassembler" for configuration "Release"
set_property(TARGET LLVMWebAssemblyDisassembler APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMWebAssemblyDisassembler PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMWebAssemblyDisassembler.a"
  )

list(APPEND _cmake_import_check_targets LLVMWebAssemblyDisassembler )
list(APPEND _cmake_import_check_files_for_LLVMWebAssemblyDisassembler "${_IMPORT_PREFIX}/lib/libLLVMWebAssemblyDisassembler.a" )

# Import target "LLVMWebAssemblyDesc" for configuration "Release"
set_property(TARGET LLVMWebAssemblyDesc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMWebAssemblyDesc PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMWebAssemblyDesc.a"
  )

list(APPEND _cmake_import_check_targets LLVMWebAssemblyDesc )
list(APPEND _cmake_import_check_files_for_LLVMWebAssemblyDesc "${_IMPORT_PREFIX}/lib/libLLVMWebAssemblyDesc.a" )

# Import target "LLVMWebAssemblyInfo" for configuration "Release"
set_property(TARGET LLVMWebAssemblyInfo APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMWebAssemblyInfo PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMWebAssemblyInfo.a"
  )

list(APPEND _cmake_import_check_targets LLVMWebAssemblyInfo )
list(APPEND _cmake_import_check_files_for_LLVMWebAssemblyInfo "${_IMPORT_PREFIX}/lib/libLLVMWebAssemblyInfo.a" )

# Import target "LLVMWebAssemblyUtils" for configuration "Release"
set_property(TARGET LLVMWebAssemblyUtils APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMWebAssemblyUtils PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMWebAssemblyUtils.a"
  )

list(APPEND _cmake_import_check_targets LLVMWebAssemblyUtils )
list(APPEND _cmake_import_check_files_for_LLVMWebAssemblyUtils "${_IMPORT_PREFIX}/lib/libLLVMWebAssemblyUtils.a" )

# Import target "LLVMX86CodeGen" for configuration "Release"
set_property(TARGET LLVMX86CodeGen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMX86CodeGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMX86CodeGen.a"
  )

list(APPEND _cmake_import_check_targets LLVMX86CodeGen )
list(APPEND _cmake_import_check_files_for_LLVMX86CodeGen "${_IMPORT_PREFIX}/lib/libLLVMX86CodeGen.a" )

# Import target "LLVMX86AsmParser" for configuration "Release"
set_property(TARGET LLVMX86AsmParser APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMX86AsmParser PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMX86AsmParser.a"
  )

list(APPEND _cmake_import_check_targets LLVMX86AsmParser )
list(APPEND _cmake_import_check_files_for_LLVMX86AsmParser "${_IMPORT_PREFIX}/lib/libLLVMX86AsmParser.a" )

# Import target "LLVMX86Disassembler" for configuration "Release"
set_property(TARGET LLVMX86Disassembler APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMX86Disassembler PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMX86Disassembler.a"
  )

list(APPEND _cmake_import_check_targets LLVMX86Disassembler )
list(APPEND _cmake_import_check_files_for_LLVMX86Disassembler "${_IMPORT_PREFIX}/lib/libLLVMX86Disassembler.a" )

# Import target "LLVMX86TargetMCA" for configuration "Release"
set_property(TARGET LLVMX86TargetMCA APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMX86TargetMCA PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMX86TargetMCA.a"
  )

list(APPEND _cmake_import_check_targets LLVMX86TargetMCA )
list(APPEND _cmake_import_check_files_for_LLVMX86TargetMCA "${_IMPORT_PREFIX}/lib/libLLVMX86TargetMCA.a" )

# Import target "LLVMX86Desc" for configuration "Release"
set_property(TARGET LLVMX86Desc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMX86Desc PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMX86Desc.a"
  )

list(APPEND _cmake_import_check_targets LLVMX86Desc )
list(APPEND _cmake_import_check_files_for_LLVMX86Desc "${_IMPORT_PREFIX}/lib/libLLVMX86Desc.a" )

# Import target "LLVMX86Info" for configuration "Release"
set_property(TARGET LLVMX86Info APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMX86Info PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMX86Info.a"
  )

list(APPEND _cmake_import_check_targets LLVMX86Info )
list(APPEND _cmake_import_check_files_for_LLVMX86Info "${_IMPORT_PREFIX}/lib/libLLVMX86Info.a" )

# Import target "LLVMXCoreCodeGen" for configuration "Release"
set_property(TARGET LLVMXCoreCodeGen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMXCoreCodeGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMXCoreCodeGen.a"
  )

list(APPEND _cmake_import_check_targets LLVMXCoreCodeGen )
list(APPEND _cmake_import_check_files_for_LLVMXCoreCodeGen "${_IMPORT_PREFIX}/lib/libLLVMXCoreCodeGen.a" )

# Import target "LLVMXCoreDisassembler" for configuration "Release"
set_property(TARGET LLVMXCoreDisassembler APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMXCoreDisassembler PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMXCoreDisassembler.a"
  )

list(APPEND _cmake_import_check_targets LLVMXCoreDisassembler )
list(APPEND _cmake_import_check_files_for_LLVMXCoreDisassembler "${_IMPORT_PREFIX}/lib/libLLVMXCoreDisassembler.a" )

# Import target "LLVMXCoreDesc" for configuration "Release"
set_property(TARGET LLVMXCoreDesc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMXCoreDesc PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMXCoreDesc.a"
  )

list(APPEND _cmake_import_check_targets LLVMXCoreDesc )
list(APPEND _cmake_import_check_files_for_LLVMXCoreDesc "${_IMPORT_PREFIX}/lib/libLLVMXCoreDesc.a" )

# Import target "LLVMXCoreInfo" for configuration "Release"
set_property(TARGET LLVMXCoreInfo APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMXCoreInfo PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMXCoreInfo.a"
  )

list(APPEND _cmake_import_check_targets LLVMXCoreInfo )
list(APPEND _cmake_import_check_files_for_LLVMXCoreInfo "${_IMPORT_PREFIX}/lib/libLLVMXCoreInfo.a" )

# Import target "LLVMSandboxIR" for configuration "Release"
set_property(TARGET LLVMSandboxIR APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMSandboxIR PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMSandboxIR.a"
  )

list(APPEND _cmake_import_check_targets LLVMSandboxIR )
list(APPEND _cmake_import_check_files_for_LLVMSandboxIR "${_IMPORT_PREFIX}/lib/libLLVMSandboxIR.a" )

# Import target "LLVMAsmParser" for configuration "Release"
set_property(TARGET LLVMAsmParser APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMAsmParser PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMAsmParser.a"
  )

list(APPEND _cmake_import_check_targets LLVMAsmParser )
list(APPEND _cmake_import_check_files_for_LLVMAsmParser "${_IMPORT_PREFIX}/lib/libLLVMAsmParser.a" )

# Import target "LLVMLineEditor" for configuration "Release"
set_property(TARGET LLVMLineEditor APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMLineEditor PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMLineEditor.a"
  )

list(APPEND _cmake_import_check_targets LLVMLineEditor )
list(APPEND _cmake_import_check_files_for_LLVMLineEditor "${_IMPORT_PREFIX}/lib/libLLVMLineEditor.a" )

# Import target "LLVMProfileData" for configuration "Release"
set_property(TARGET LLVMProfileData APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMProfileData PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMProfileData.a"
  )

list(APPEND _cmake_import_check_targets LLVMProfileData )
list(APPEND _cmake_import_check_files_for_LLVMProfileData "${_IMPORT_PREFIX}/lib/libLLVMProfileData.a" )

# Import target "LLVMCoverage" for configuration "Release"
set_property(TARGET LLVMCoverage APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMCoverage PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMCoverage.a"
  )

list(APPEND _cmake_import_check_targets LLVMCoverage )
list(APPEND _cmake_import_check_files_for_LLVMCoverage "${_IMPORT_PREFIX}/lib/libLLVMCoverage.a" )

# Import target "LLVMPasses" for configuration "Release"
set_property(TARGET LLVMPasses APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMPasses PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMPasses.a"
  )

list(APPEND _cmake_import_check_targets LLVMPasses )
list(APPEND _cmake_import_check_files_for_LLVMPasses "${_IMPORT_PREFIX}/lib/libLLVMPasses.a" )

# Import target "LLVMTargetParser" for configuration "Release"
set_property(TARGET LLVMTargetParser APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMTargetParser PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMTargetParser.a"
  )

list(APPEND _cmake_import_check_targets LLVMTargetParser )
list(APPEND _cmake_import_check_files_for_LLVMTargetParser "${_IMPORT_PREFIX}/lib/libLLVMTargetParser.a" )

# Import target "LLVMTextAPI" for configuration "Release"
set_property(TARGET LLVMTextAPI APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMTextAPI PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMTextAPI.a"
  )

list(APPEND _cmake_import_check_targets LLVMTextAPI )
list(APPEND _cmake_import_check_files_for_LLVMTextAPI "${_IMPORT_PREFIX}/lib/libLLVMTextAPI.a" )

# Import target "LLVMTextAPIBinaryReader" for configuration "Release"
set_property(TARGET LLVMTextAPIBinaryReader APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMTextAPIBinaryReader PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMTextAPIBinaryReader.a"
  )

list(APPEND _cmake_import_check_targets LLVMTextAPIBinaryReader )
list(APPEND _cmake_import_check_files_for_LLVMTextAPIBinaryReader "${_IMPORT_PREFIX}/lib/libLLVMTextAPIBinaryReader.a" )

# Import target "LLVMTelemetry" for configuration "Release"
set_property(TARGET LLVMTelemetry APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMTelemetry PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMTelemetry.a"
  )

list(APPEND _cmake_import_check_targets LLVMTelemetry )
list(APPEND _cmake_import_check_files_for_LLVMTelemetry "${_IMPORT_PREFIX}/lib/libLLVMTelemetry.a" )

# Import target "LLVMDlltoolDriver" for configuration "Release"
set_property(TARGET LLVMDlltoolDriver APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMDlltoolDriver PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMDlltoolDriver.a"
  )

list(APPEND _cmake_import_check_targets LLVMDlltoolDriver )
list(APPEND _cmake_import_check_files_for_LLVMDlltoolDriver "${_IMPORT_PREFIX}/lib/libLLVMDlltoolDriver.a" )

# Import target "LLVMLibDriver" for configuration "Release"
set_property(TARGET LLVMLibDriver APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMLibDriver PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMLibDriver.a"
  )

list(APPEND _cmake_import_check_targets LLVMLibDriver )
list(APPEND _cmake_import_check_files_for_LLVMLibDriver "${_IMPORT_PREFIX}/lib/libLLVMLibDriver.a" )

# Import target "LLVMXRay" for configuration "Release"
set_property(TARGET LLVMXRay APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMXRay PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMXRay.a"
  )

list(APPEND _cmake_import_check_targets LLVMXRay )
list(APPEND _cmake_import_check_files_for_LLVMXRay "${_IMPORT_PREFIX}/lib/libLLVMXRay.a" )

# Import target "LLVMWindowsDriver" for configuration "Release"
set_property(TARGET LLVMWindowsDriver APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMWindowsDriver PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMWindowsDriver.a"
  )

list(APPEND _cmake_import_check_targets LLVMWindowsDriver )
list(APPEND _cmake_import_check_files_for_LLVMWindowsDriver "${_IMPORT_PREFIX}/lib/libLLVMWindowsDriver.a" )

# Import target "LLVMWindowsManifest" for configuration "Release"
set_property(TARGET LLVMWindowsManifest APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMWindowsManifest PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMWindowsManifest.a"
  )

list(APPEND _cmake_import_check_targets LLVMWindowsManifest )
list(APPEND _cmake_import_check_files_for_LLVMWindowsManifest "${_IMPORT_PREFIX}/lib/libLLVMWindowsManifest.a" )

# Import target "FileCheck" for configuration "Release"
set_property(TARGET FileCheck APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(FileCheck PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/FileCheck.exe"
  )

list(APPEND _cmake_import_check_targets FileCheck )
list(APPEND _cmake_import_check_files_for_FileCheck "${_IMPORT_PREFIX}/bin/FileCheck.exe" )

# Import target "llvm-PerfectShuffle" for configuration "Release"
set_property(TARGET llvm-PerfectShuffle APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-PerfectShuffle PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-PerfectShuffle.exe"
  )

list(APPEND _cmake_import_check_targets llvm-PerfectShuffle )
list(APPEND _cmake_import_check_files_for_llvm-PerfectShuffle "${_IMPORT_PREFIX}/bin/llvm-PerfectShuffle.exe" )

# Import target "count" for configuration "Release"
set_property(TARGET count APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(count PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/count.exe"
  )

list(APPEND _cmake_import_check_targets count )
list(APPEND _cmake_import_check_files_for_count "${_IMPORT_PREFIX}/bin/count.exe" )

# Import target "not" for configuration "Release"
set_property(TARGET not APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(not PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/not.exe"
  )

list(APPEND _cmake_import_check_targets not )
list(APPEND _cmake_import_check_files_for_not "${_IMPORT_PREFIX}/bin/not.exe" )

# Import target "UnicodeNameMappingGenerator" for configuration "Release"
set_property(TARGET UnicodeNameMappingGenerator APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(UnicodeNameMappingGenerator PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/UnicodeNameMappingGenerator.exe"
  )

list(APPEND _cmake_import_check_targets UnicodeNameMappingGenerator )
list(APPEND _cmake_import_check_files_for_UnicodeNameMappingGenerator "${_IMPORT_PREFIX}/bin/UnicodeNameMappingGenerator.exe" )

# Import target "yaml-bench" for configuration "Release"
set_property(TARGET yaml-bench APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(yaml-bench PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/yaml-bench.exe"
  )

list(APPEND _cmake_import_check_targets yaml-bench )
list(APPEND _cmake_import_check_files_for_yaml-bench "${_IMPORT_PREFIX}/bin/yaml-bench.exe" )

# Import target "split-file" for configuration "Release"
set_property(TARGET split-file APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(split-file PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/split-file.exe"
  )

list(APPEND _cmake_import_check_targets split-file )
list(APPEND _cmake_import_check_files_for_split-file "${_IMPORT_PREFIX}/bin/split-file.exe" )

# Import target "LTO" for configuration "Release"
set_property(TARGET LTO APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LTO PROPERTIES
  IMPORTED_IMPLIB_RELEASE "${_IMPORT_PREFIX}/lib/libLTO.dll.a"
  IMPORTED_LINK_DEPENDENT_LIBRARIES_RELEASE "LLVM"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/libLTO.dll"
  )

list(APPEND _cmake_import_check_targets LTO )
list(APPEND _cmake_import_check_files_for_LTO "${_IMPORT_PREFIX}/lib/libLTO.dll.a" "${_IMPORT_PREFIX}/bin/libLTO.dll" )

# Import target "llvm-ar" for configuration "Release"
set_property(TARGET llvm-ar APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-ar PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-ar.exe"
  )

list(APPEND _cmake_import_check_targets llvm-ar )
list(APPEND _cmake_import_check_files_for_llvm-ar "${_IMPORT_PREFIX}/bin/llvm-ar.exe" )

# Import target "llvm-config" for configuration "Release"
set_property(TARGET llvm-config APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-config PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-config.exe"
  )

list(APPEND _cmake_import_check_targets llvm-config )
list(APPEND _cmake_import_check_files_for_llvm-config "${_IMPORT_PREFIX}/bin/llvm-config.exe" )

# Import target "llvm-ctxprof-util" for configuration "Release"
set_property(TARGET llvm-ctxprof-util APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-ctxprof-util PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-ctxprof-util.exe"
  )

list(APPEND _cmake_import_check_targets llvm-ctxprof-util )
list(APPEND _cmake_import_check_files_for_llvm-ctxprof-util "${_IMPORT_PREFIX}/bin/llvm-ctxprof-util.exe" )

# Import target "llvm-lto" for configuration "Release"
set_property(TARGET llvm-lto APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-lto PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-lto.exe"
  )

list(APPEND _cmake_import_check_targets llvm-lto )
list(APPEND _cmake_import_check_files_for_llvm-lto "${_IMPORT_PREFIX}/bin/llvm-lto.exe" )

# Import target "llvm-profdata" for configuration "Release"
set_property(TARGET llvm-profdata APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-profdata PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-profdata.exe"
  )

list(APPEND _cmake_import_check_targets llvm-profdata )
list(APPEND _cmake_import_check_files_for_llvm-profdata "${_IMPORT_PREFIX}/bin/llvm-profdata.exe" )

# Import target "bugpoint" for configuration "Release"
set_property(TARGET bugpoint APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(bugpoint PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/bugpoint.exe"
  )

list(APPEND _cmake_import_check_targets bugpoint )
list(APPEND _cmake_import_check_files_for_bugpoint "${_IMPORT_PREFIX}/bin/bugpoint.exe" )

# Import target "dsymutil" for configuration "Release"
set_property(TARGET dsymutil APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(dsymutil PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/dsymutil.exe"
  )

list(APPEND _cmake_import_check_targets dsymutil )
list(APPEND _cmake_import_check_files_for_dsymutil "${_IMPORT_PREFIX}/bin/dsymutil.exe" )

# Import target "llc" for configuration "Release"
set_property(TARGET llc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llc PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llc.exe"
  )

list(APPEND _cmake_import_check_targets llc )
list(APPEND _cmake_import_check_files_for_llc "${_IMPORT_PREFIX}/bin/llc.exe" )

# Import target "lli-child-target" for configuration "Release"
set_property(TARGET lli-child-target APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(lli-child-target PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/lli-child-target.exe"
  )

list(APPEND _cmake_import_check_targets lli-child-target )
list(APPEND _cmake_import_check_files_for_lli-child-target "${_IMPORT_PREFIX}/bin/lli-child-target.exe" )

# Import target "lli" for configuration "Release"
set_property(TARGET lli APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(lli PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/lli.exe"
  )

list(APPEND _cmake_import_check_targets lli )
list(APPEND _cmake_import_check_files_for_lli "${_IMPORT_PREFIX}/bin/lli.exe" )

# Import target "llvm-as" for configuration "Release"
set_property(TARGET llvm-as APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-as PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-as.exe"
  )

list(APPEND _cmake_import_check_targets llvm-as )
list(APPEND _cmake_import_check_files_for_llvm-as "${_IMPORT_PREFIX}/bin/llvm-as.exe" )

# Import target "llvm-bcanalyzer" for configuration "Release"
set_property(TARGET llvm-bcanalyzer APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-bcanalyzer PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-bcanalyzer.exe"
  )

list(APPEND _cmake_import_check_targets llvm-bcanalyzer )
list(APPEND _cmake_import_check_files_for_llvm-bcanalyzer "${_IMPORT_PREFIX}/bin/llvm-bcanalyzer.exe" )

# Import target "llvm-c-test" for configuration "Release"
set_property(TARGET llvm-c-test APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-c-test PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-c-test.exe"
  )

list(APPEND _cmake_import_check_targets llvm-c-test )
list(APPEND _cmake_import_check_files_for_llvm-c-test "${_IMPORT_PREFIX}/bin/llvm-c-test.exe" )

# Import target "llvm-cat" for configuration "Release"
set_property(TARGET llvm-cat APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-cat PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-cat.exe"
  )

list(APPEND _cmake_import_check_targets llvm-cat )
list(APPEND _cmake_import_check_files_for_llvm-cat "${_IMPORT_PREFIX}/bin/llvm-cat.exe" )

# Import target "llvm-cfi-verify" for configuration "Release"
set_property(TARGET llvm-cfi-verify APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-cfi-verify PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-cfi-verify.exe"
  )

list(APPEND _cmake_import_check_targets llvm-cfi-verify )
list(APPEND _cmake_import_check_files_for_llvm-cfi-verify "${_IMPORT_PREFIX}/bin/llvm-cfi-verify.exe" )

# Import target "LLVMCFIVerify" for configuration "Release"
set_property(TARGET LLVMCFIVerify APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMCFIVerify PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMCFIVerify.a"
  )

list(APPEND _cmake_import_check_targets LLVMCFIVerify )
list(APPEND _cmake_import_check_files_for_LLVMCFIVerify "${_IMPORT_PREFIX}/lib/libLLVMCFIVerify.a" )

# Import target "llvm-cgdata" for configuration "Release"
set_property(TARGET llvm-cgdata APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-cgdata PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-cgdata.exe"
  )

list(APPEND _cmake_import_check_targets llvm-cgdata )
list(APPEND _cmake_import_check_files_for_llvm-cgdata "${_IMPORT_PREFIX}/bin/llvm-cgdata.exe" )

# Import target "llvm-cov" for configuration "Release"
set_property(TARGET llvm-cov APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-cov PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-cov.exe"
  )

list(APPEND _cmake_import_check_targets llvm-cov )
list(APPEND _cmake_import_check_files_for_llvm-cov "${_IMPORT_PREFIX}/bin/llvm-cov.exe" )

# Import target "llvm-cvtres" for configuration "Release"
set_property(TARGET llvm-cvtres APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-cvtres PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-cvtres.exe"
  )

list(APPEND _cmake_import_check_targets llvm-cvtres )
list(APPEND _cmake_import_check_files_for_llvm-cvtres "${_IMPORT_PREFIX}/bin/llvm-cvtres.exe" )

# Import target "llvm-cxxdump" for configuration "Release"
set_property(TARGET llvm-cxxdump APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-cxxdump PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-cxxdump.exe"
  )

list(APPEND _cmake_import_check_targets llvm-cxxdump )
list(APPEND _cmake_import_check_files_for_llvm-cxxdump "${_IMPORT_PREFIX}/bin/llvm-cxxdump.exe" )

# Import target "llvm-cxxfilt" for configuration "Release"
set_property(TARGET llvm-cxxfilt APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-cxxfilt PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-cxxfilt.exe"
  )

list(APPEND _cmake_import_check_targets llvm-cxxfilt )
list(APPEND _cmake_import_check_files_for_llvm-cxxfilt "${_IMPORT_PREFIX}/bin/llvm-cxxfilt.exe" )

# Import target "llvm-cxxmap" for configuration "Release"
set_property(TARGET llvm-cxxmap APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-cxxmap PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-cxxmap.exe"
  )

list(APPEND _cmake_import_check_targets llvm-cxxmap )
list(APPEND _cmake_import_check_files_for_llvm-cxxmap "${_IMPORT_PREFIX}/bin/llvm-cxxmap.exe" )

# Import target "llvm-debuginfo-analyzer" for configuration "Release"
set_property(TARGET llvm-debuginfo-analyzer APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-debuginfo-analyzer PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-debuginfo-analyzer.exe"
  )

list(APPEND _cmake_import_check_targets llvm-debuginfo-analyzer )
list(APPEND _cmake_import_check_files_for_llvm-debuginfo-analyzer "${_IMPORT_PREFIX}/bin/llvm-debuginfo-analyzer.exe" )

# Import target "llvm-debuginfod" for configuration "Release"
set_property(TARGET llvm-debuginfod APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-debuginfod PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-debuginfod.exe"
  )

list(APPEND _cmake_import_check_targets llvm-debuginfod )
list(APPEND _cmake_import_check_files_for_llvm-debuginfod "${_IMPORT_PREFIX}/bin/llvm-debuginfod.exe" )

# Import target "llvm-debuginfod-find" for configuration "Release"
set_property(TARGET llvm-debuginfod-find APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-debuginfod-find PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-debuginfod-find.exe"
  )

list(APPEND _cmake_import_check_targets llvm-debuginfod-find )
list(APPEND _cmake_import_check_files_for_llvm-debuginfod-find "${_IMPORT_PREFIX}/bin/llvm-debuginfod-find.exe" )

# Import target "llvm-diff" for configuration "Release"
set_property(TARGET llvm-diff APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-diff PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-diff.exe"
  )

list(APPEND _cmake_import_check_targets llvm-diff )
list(APPEND _cmake_import_check_files_for_llvm-diff "${_IMPORT_PREFIX}/bin/llvm-diff.exe" )

# Import target "LLVMDiff" for configuration "Release"
set_property(TARGET LLVMDiff APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMDiff PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMDiff.a"
  )

list(APPEND _cmake_import_check_targets LLVMDiff )
list(APPEND _cmake_import_check_files_for_LLVMDiff "${_IMPORT_PREFIX}/lib/libLLVMDiff.a" )

# Import target "llvm-dis" for configuration "Release"
set_property(TARGET llvm-dis APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-dis PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-dis.exe"
  )

list(APPEND _cmake_import_check_targets llvm-dis )
list(APPEND _cmake_import_check_files_for_llvm-dis "${_IMPORT_PREFIX}/bin/llvm-dis.exe" )

# Import target "llvm-dwarfdump" for configuration "Release"
set_property(TARGET llvm-dwarfdump APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-dwarfdump PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-dwarfdump.exe"
  )

list(APPEND _cmake_import_check_targets llvm-dwarfdump )
list(APPEND _cmake_import_check_files_for_llvm-dwarfdump "${_IMPORT_PREFIX}/bin/llvm-dwarfdump.exe" )

# Import target "llvm-dwarfutil" for configuration "Release"
set_property(TARGET llvm-dwarfutil APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-dwarfutil PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-dwarfutil.exe"
  )

list(APPEND _cmake_import_check_targets llvm-dwarfutil )
list(APPEND _cmake_import_check_files_for_llvm-dwarfutil "${_IMPORT_PREFIX}/bin/llvm-dwarfutil.exe" )

# Import target "llvm-dwp" for configuration "Release"
set_property(TARGET llvm-dwp APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-dwp PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-dwp.exe"
  )

list(APPEND _cmake_import_check_targets llvm-dwp )
list(APPEND _cmake_import_check_files_for_llvm-dwp "${_IMPORT_PREFIX}/bin/llvm-dwp.exe" )

# Import target "LLVMExegesisX86" for configuration "Release"
set_property(TARGET LLVMExegesisX86 APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMExegesisX86 PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMExegesisX86.a"
  )

list(APPEND _cmake_import_check_targets LLVMExegesisX86 )
list(APPEND _cmake_import_check_files_for_LLVMExegesisX86 "${_IMPORT_PREFIX}/lib/libLLVMExegesisX86.a" )

# Import target "LLVMExegesisAArch64" for configuration "Release"
set_property(TARGET LLVMExegesisAArch64 APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMExegesisAArch64 PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMExegesisAArch64.a"
  )

list(APPEND _cmake_import_check_targets LLVMExegesisAArch64 )
list(APPEND _cmake_import_check_files_for_LLVMExegesisAArch64 "${_IMPORT_PREFIX}/lib/libLLVMExegesisAArch64.a" )

# Import target "LLVMExegesisPowerPC" for configuration "Release"
set_property(TARGET LLVMExegesisPowerPC APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMExegesisPowerPC PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMExegesisPowerPC.a"
  )

list(APPEND _cmake_import_check_targets LLVMExegesisPowerPC )
list(APPEND _cmake_import_check_files_for_LLVMExegesisPowerPC "${_IMPORT_PREFIX}/lib/libLLVMExegesisPowerPC.a" )

# Import target "LLVMExegesisMips" for configuration "Release"
set_property(TARGET LLVMExegesisMips APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMExegesisMips PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMExegesisMips.a"
  )

list(APPEND _cmake_import_check_targets LLVMExegesisMips )
list(APPEND _cmake_import_check_files_for_LLVMExegesisMips "${_IMPORT_PREFIX}/lib/libLLVMExegesisMips.a" )

# Import target "LLVMExegesisRISCV" for configuration "Release"
set_property(TARGET LLVMExegesisRISCV APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMExegesisRISCV PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMExegesisRISCV.a"
  )

list(APPEND _cmake_import_check_targets LLVMExegesisRISCV )
list(APPEND _cmake_import_check_files_for_LLVMExegesisRISCV "${_IMPORT_PREFIX}/lib/libLLVMExegesisRISCV.a" )

# Import target "LLVMExegesis" for configuration "Release"
set_property(TARGET LLVMExegesis APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMExegesis PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMExegesis.a"
  )

list(APPEND _cmake_import_check_targets LLVMExegesis )
list(APPEND _cmake_import_check_files_for_LLVMExegesis "${_IMPORT_PREFIX}/lib/libLLVMExegesis.a" )

# Import target "llvm-exegesis" for configuration "Release"
set_property(TARGET llvm-exegesis APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-exegesis PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-exegesis.exe"
  )

list(APPEND _cmake_import_check_targets llvm-exegesis )
list(APPEND _cmake_import_check_files_for_llvm-exegesis "${_IMPORT_PREFIX}/bin/llvm-exegesis.exe" )

# Import target "llvm-extract" for configuration "Release"
set_property(TARGET llvm-extract APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-extract PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-extract.exe"
  )

list(APPEND _cmake_import_check_targets llvm-extract )
list(APPEND _cmake_import_check_files_for_llvm-extract "${_IMPORT_PREFIX}/bin/llvm-extract.exe" )

# Import target "llvm-gsymutil" for configuration "Release"
set_property(TARGET llvm-gsymutil APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-gsymutil PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-gsymutil.exe"
  )

list(APPEND _cmake_import_check_targets llvm-gsymutil )
list(APPEND _cmake_import_check_files_for_llvm-gsymutil "${_IMPORT_PREFIX}/bin/llvm-gsymutil.exe" )

# Import target "llvm-ifs" for configuration "Release"
set_property(TARGET llvm-ifs APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-ifs PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-ifs.exe"
  )

list(APPEND _cmake_import_check_targets llvm-ifs )
list(APPEND _cmake_import_check_files_for_llvm-ifs "${_IMPORT_PREFIX}/bin/llvm-ifs.exe" )

# Import target "llvm-jitlink-executor" for configuration "Release"
set_property(TARGET llvm-jitlink-executor APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-jitlink-executor PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-jitlink-executor.exe"
  )

list(APPEND _cmake_import_check_targets llvm-jitlink-executor )
list(APPEND _cmake_import_check_files_for_llvm-jitlink-executor "${_IMPORT_PREFIX}/bin/llvm-jitlink-executor.exe" )

# Import target "llvm-jitlink" for configuration "Release"
set_property(TARGET llvm-jitlink APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-jitlink PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-jitlink.exe"
  )

list(APPEND _cmake_import_check_targets llvm-jitlink )
list(APPEND _cmake_import_check_files_for_llvm-jitlink "${_IMPORT_PREFIX}/bin/llvm-jitlink.exe" )

# Import target "llvm-libtool-darwin" for configuration "Release"
set_property(TARGET llvm-libtool-darwin APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-libtool-darwin PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-libtool-darwin.exe"
  )

list(APPEND _cmake_import_check_targets llvm-libtool-darwin )
list(APPEND _cmake_import_check_files_for_llvm-libtool-darwin "${_IMPORT_PREFIX}/bin/llvm-libtool-darwin.exe" )

# Import target "llvm-link" for configuration "Release"
set_property(TARGET llvm-link APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-link PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-link.exe"
  )

list(APPEND _cmake_import_check_targets llvm-link )
list(APPEND _cmake_import_check_files_for_llvm-link "${_IMPORT_PREFIX}/bin/llvm-link.exe" )

# Import target "llvm-lipo" for configuration "Release"
set_property(TARGET llvm-lipo APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-lipo PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-lipo.exe"
  )

list(APPEND _cmake_import_check_targets llvm-lipo )
list(APPEND _cmake_import_check_files_for_llvm-lipo "${_IMPORT_PREFIX}/bin/llvm-lipo.exe" )

# Import target "llvm-lto2" for configuration "Release"
set_property(TARGET llvm-lto2 APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-lto2 PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-lto2.exe"
  )

list(APPEND _cmake_import_check_targets llvm-lto2 )
list(APPEND _cmake_import_check_files_for_llvm-lto2 "${_IMPORT_PREFIX}/bin/llvm-lto2.exe" )

# Import target "llvm-mc" for configuration "Release"
set_property(TARGET llvm-mc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-mc PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-mc.exe"
  )

list(APPEND _cmake_import_check_targets llvm-mc )
list(APPEND _cmake_import_check_files_for_llvm-mc "${_IMPORT_PREFIX}/bin/llvm-mc.exe" )

# Import target "llvm-mca" for configuration "Release"
set_property(TARGET llvm-mca APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-mca PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-mca.exe"
  )

list(APPEND _cmake_import_check_targets llvm-mca )
list(APPEND _cmake_import_check_files_for_llvm-mca "${_IMPORT_PREFIX}/bin/llvm-mca.exe" )

# Import target "llvm-ml" for configuration "Release"
set_property(TARGET llvm-ml APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-ml PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-ml.exe"
  )

list(APPEND _cmake_import_check_targets llvm-ml )
list(APPEND _cmake_import_check_files_for_llvm-ml "${_IMPORT_PREFIX}/bin/llvm-ml.exe" )

# Import target "llvm-modextract" for configuration "Release"
set_property(TARGET llvm-modextract APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-modextract PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-modextract.exe"
  )

list(APPEND _cmake_import_check_targets llvm-modextract )
list(APPEND _cmake_import_check_files_for_llvm-modextract "${_IMPORT_PREFIX}/bin/llvm-modextract.exe" )

# Import target "llvm-mt" for configuration "Release"
set_property(TARGET llvm-mt APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-mt PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-mt.exe"
  )

list(APPEND _cmake_import_check_targets llvm-mt )
list(APPEND _cmake_import_check_files_for_llvm-mt "${_IMPORT_PREFIX}/bin/llvm-mt.exe" )

# Import target "llvm-nm" for configuration "Release"
set_property(TARGET llvm-nm APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-nm PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-nm.exe"
  )

list(APPEND _cmake_import_check_targets llvm-nm )
list(APPEND _cmake_import_check_files_for_llvm-nm "${_IMPORT_PREFIX}/bin/llvm-nm.exe" )

# Import target "llvm-objcopy" for configuration "Release"
set_property(TARGET llvm-objcopy APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-objcopy PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-objcopy.exe"
  )

list(APPEND _cmake_import_check_targets llvm-objcopy )
list(APPEND _cmake_import_check_files_for_llvm-objcopy "${_IMPORT_PREFIX}/bin/llvm-objcopy.exe" )

# Import target "llvm-objdump" for configuration "Release"
set_property(TARGET llvm-objdump APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-objdump PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-objdump.exe"
  )

list(APPEND _cmake_import_check_targets llvm-objdump )
list(APPEND _cmake_import_check_files_for_llvm-objdump "${_IMPORT_PREFIX}/bin/llvm-objdump.exe" )

# Import target "llvm-opt-report" for configuration "Release"
set_property(TARGET llvm-opt-report APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-opt-report PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-opt-report.exe"
  )

list(APPEND _cmake_import_check_targets llvm-opt-report )
list(APPEND _cmake_import_check_files_for_llvm-opt-report "${_IMPORT_PREFIX}/bin/llvm-opt-report.exe" )

# Import target "llvm-pdbutil" for configuration "Release"
set_property(TARGET llvm-pdbutil APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-pdbutil PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-pdbutil.exe"
  )

list(APPEND _cmake_import_check_targets llvm-pdbutil )
list(APPEND _cmake_import_check_files_for_llvm-pdbutil "${_IMPORT_PREFIX}/bin/llvm-pdbutil.exe" )

# Import target "llvm-profgen" for configuration "Release"
set_property(TARGET llvm-profgen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-profgen PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-profgen.exe"
  )

list(APPEND _cmake_import_check_targets llvm-profgen )
list(APPEND _cmake_import_check_files_for_llvm-profgen "${_IMPORT_PREFIX}/bin/llvm-profgen.exe" )

# Import target "llvm-rc" for configuration "Release"
set_property(TARGET llvm-rc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-rc PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-rc.exe"
  )

list(APPEND _cmake_import_check_targets llvm-rc )
list(APPEND _cmake_import_check_files_for_llvm-rc "${_IMPORT_PREFIX}/bin/llvm-rc.exe" )

# Import target "llvm-readobj" for configuration "Release"
set_property(TARGET llvm-readobj APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-readobj PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-readobj.exe"
  )

list(APPEND _cmake_import_check_targets llvm-readobj )
list(APPEND _cmake_import_check_files_for_llvm-readobj "${_IMPORT_PREFIX}/bin/llvm-readobj.exe" )

# Import target "llvm-readtapi" for configuration "Release"
set_property(TARGET llvm-readtapi APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-readtapi PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-readtapi.exe"
  )

list(APPEND _cmake_import_check_targets llvm-readtapi )
list(APPEND _cmake_import_check_files_for_llvm-readtapi "${_IMPORT_PREFIX}/bin/llvm-readtapi.exe" )

# Import target "llvm-reduce" for configuration "Release"
set_property(TARGET llvm-reduce APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-reduce PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-reduce.exe"
  )

list(APPEND _cmake_import_check_targets llvm-reduce )
list(APPEND _cmake_import_check_files_for_llvm-reduce "${_IMPORT_PREFIX}/bin/llvm-reduce.exe" )

# Import target "llvm-remarkutil" for configuration "Release"
set_property(TARGET llvm-remarkutil APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-remarkutil PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-remarkutil.exe"
  )

list(APPEND _cmake_import_check_targets llvm-remarkutil )
list(APPEND _cmake_import_check_files_for_llvm-remarkutil "${_IMPORT_PREFIX}/bin/llvm-remarkutil.exe" )

# Import target "llvm-rtdyld" for configuration "Release"
set_property(TARGET llvm-rtdyld APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-rtdyld PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-rtdyld.exe"
  )

list(APPEND _cmake_import_check_targets llvm-rtdyld )
list(APPEND _cmake_import_check_files_for_llvm-rtdyld "${_IMPORT_PREFIX}/bin/llvm-rtdyld.exe" )

# Import target "LLVM" for configuration "Release"
set_property(TARGET LLVM APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVM PROPERTIES
  IMPORTED_IMPLIB_RELEASE "${_IMPORT_PREFIX}/lib/libLLVM-20.dll.a"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/libLLVM-20.dll"
  )

list(APPEND _cmake_import_check_targets LLVM )
list(APPEND _cmake_import_check_files_for_LLVM "${_IMPORT_PREFIX}/lib/libLLVM-20.dll.a" "${_IMPORT_PREFIX}/bin/libLLVM-20.dll" )

# Import target "llvm-sim" for configuration "Release"
set_property(TARGET llvm-sim APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-sim PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-sim.exe"
  )

list(APPEND _cmake_import_check_targets llvm-sim )
list(APPEND _cmake_import_check_files_for_llvm-sim "${_IMPORT_PREFIX}/bin/llvm-sim.exe" )

# Import target "llvm-size" for configuration "Release"
set_property(TARGET llvm-size APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-size PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-size.exe"
  )

list(APPEND _cmake_import_check_targets llvm-size )
list(APPEND _cmake_import_check_files_for_llvm-size "${_IMPORT_PREFIX}/bin/llvm-size.exe" )

# Import target "llvm-split" for configuration "Release"
set_property(TARGET llvm-split APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-split PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-split.exe"
  )

list(APPEND _cmake_import_check_targets llvm-split )
list(APPEND _cmake_import_check_files_for_llvm-split "${_IMPORT_PREFIX}/bin/llvm-split.exe" )

# Import target "llvm-stress" for configuration "Release"
set_property(TARGET llvm-stress APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-stress PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-stress.exe"
  )

list(APPEND _cmake_import_check_targets llvm-stress )
list(APPEND _cmake_import_check_files_for_llvm-stress "${_IMPORT_PREFIX}/bin/llvm-stress.exe" )

# Import target "llvm-strings" for configuration "Release"
set_property(TARGET llvm-strings APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-strings PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-strings.exe"
  )

list(APPEND _cmake_import_check_targets llvm-strings )
list(APPEND _cmake_import_check_files_for_llvm-strings "${_IMPORT_PREFIX}/bin/llvm-strings.exe" )

# Import target "llvm-symbolizer" for configuration "Release"
set_property(TARGET llvm-symbolizer APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-symbolizer PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-symbolizer.exe"
  )

list(APPEND _cmake_import_check_targets llvm-symbolizer )
list(APPEND _cmake_import_check_files_for_llvm-symbolizer "${_IMPORT_PREFIX}/bin/llvm-symbolizer.exe" )

# Import target "llvm-tli-checker" for configuration "Release"
set_property(TARGET llvm-tli-checker APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-tli-checker PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-tli-checker.exe"
  )

list(APPEND _cmake_import_check_targets llvm-tli-checker )
list(APPEND _cmake_import_check_files_for_llvm-tli-checker "${_IMPORT_PREFIX}/bin/llvm-tli-checker.exe" )

# Import target "llvm-undname" for configuration "Release"
set_property(TARGET llvm-undname APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-undname PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-undname.exe"
  )

list(APPEND _cmake_import_check_targets llvm-undname )
list(APPEND _cmake_import_check_files_for_llvm-undname "${_IMPORT_PREFIX}/bin/llvm-undname.exe" )

# Import target "llvm-xray" for configuration "Release"
set_property(TARGET llvm-xray APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(llvm-xray PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/llvm-xray.exe"
  )

list(APPEND _cmake_import_check_targets llvm-xray )
list(APPEND _cmake_import_check_files_for_llvm-xray "${_IMPORT_PREFIX}/bin/llvm-xray.exe" )

# Import target "obj2yaml" for configuration "Release"
set_property(TARGET obj2yaml APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(obj2yaml PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/obj2yaml.exe"
  )

list(APPEND _cmake_import_check_targets obj2yaml )
list(APPEND _cmake_import_check_files_for_obj2yaml "${_IMPORT_PREFIX}/bin/obj2yaml.exe" )

# Import target "LLVMOptDriver" for configuration "Release"
set_property(TARGET LLVMOptDriver APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LLVMOptDriver PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libLLVMOptDriver.a"
  )

list(APPEND _cmake_import_check_targets LLVMOptDriver )
list(APPEND _cmake_import_check_files_for_LLVMOptDriver "${_IMPORT_PREFIX}/lib/libLLVMOptDriver.a" )

# Import target "opt" for configuration "Release"
set_property(TARGET opt APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(opt PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/opt.exe"
  )

list(APPEND _cmake_import_check_targets opt )
list(APPEND _cmake_import_check_files_for_opt "${_IMPORT_PREFIX}/bin/opt.exe" )

# Import target "reduce-chunk-list" for configuration "Release"
set_property(TARGET reduce-chunk-list APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(reduce-chunk-list PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/reduce-chunk-list.exe"
  )

list(APPEND _cmake_import_check_targets reduce-chunk-list )
list(APPEND _cmake_import_check_files_for_reduce-chunk-list "${_IMPORT_PREFIX}/bin/reduce-chunk-list.exe" )

# Import target "Remarks" for configuration "Release"
set_property(TARGET Remarks APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(Remarks PROPERTIES
  IMPORTED_IMPLIB_RELEASE "${_IMPORT_PREFIX}/lib/libRemarks.dll.a"
  IMPORTED_LINK_DEPENDENT_LIBRARIES_RELEASE "LLVM"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/libRemarks.dll"
  )

list(APPEND _cmake_import_check_targets Remarks )
list(APPEND _cmake_import_check_files_for_Remarks "${_IMPORT_PREFIX}/lib/libRemarks.dll.a" "${_IMPORT_PREFIX}/bin/libRemarks.dll" )

# Import target "sancov" for configuration "Release"
set_property(TARGET sancov APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(sancov PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/sancov.exe"
  )

list(APPEND _cmake_import_check_targets sancov )
list(APPEND _cmake_import_check_files_for_sancov "${_IMPORT_PREFIX}/bin/sancov.exe" )

# Import target "sanstats" for configuration "Release"
set_property(TARGET sanstats APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(sanstats PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/sanstats.exe"
  )

list(APPEND _cmake_import_check_targets sanstats )
list(APPEND _cmake_import_check_files_for_sanstats "${_IMPORT_PREFIX}/bin/sanstats.exe" )

# Import target "verify-uselistorder" for configuration "Release"
set_property(TARGET verify-uselistorder APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(verify-uselistorder PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/verify-uselistorder.exe"
  )

list(APPEND _cmake_import_check_targets verify-uselistorder )
list(APPEND _cmake_import_check_files_for_verify-uselistorder "${_IMPORT_PREFIX}/bin/verify-uselistorder.exe" )

# Import target "yaml2obj" for configuration "Release"
set_property(TARGET yaml2obj APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(yaml2obj PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/yaml2obj.exe"
  )

list(APPEND _cmake_import_check_targets yaml2obj )
list(APPEND _cmake_import_check_files_for_yaml2obj "${_IMPORT_PREFIX}/bin/yaml2obj.exe" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
