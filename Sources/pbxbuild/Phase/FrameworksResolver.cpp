/**
 Copyright (c) 2015-present, Facebook, Inc.
 All rights reserved.

 This source code is licensed under the BSD-style license found in the
 LICENSE file in the root directory of this source tree. An additional grant
 of patent rights can be found in the PATENTS file in the same directory.
 */

#include <pbxbuild/Phase/FrameworksResolver.h>
#include <pbxbuild/Phase/Environment.h>
#include <pbxbuild/Phase/Context.h>
#include <pbxbuild/Phase/File.h>
#include <pbxbuild/Phase/SourcesResolver.h>
#include <pbxbuild/TypeResolvedFile.h>
#include <pbxbuild/Target/Environment.h>
#include <pbxbuild/Build/Environment.h>
#include <pbxbuild/Build/Context.h>
#include <pbxbuild/Tool/ToolResolver.h>
#include <pbxbuild/Tool/LinkerResolver.h>
#include <pbxbuild/Tool/CompilationInfo.h>

namespace Phase = pbxbuild::Phase;
namespace Tool = pbxbuild::Tool;
using pbxbuild::TypeResolvedFile;
using libutil::FSUtil;

Phase::FrameworksResolver::
FrameworksResolver(pbxproj::PBX::FrameworksBuildPhase::shared_ptr const &buildPhase) :
    _buildPhase(buildPhase)
{
}

Phase::FrameworksResolver::
~FrameworksResolver()
{
}

bool Phase::FrameworksResolver::
resolve(Phase::Environment const &phaseEnvironment, Phase::Context *phaseContext)
{
    Build::Environment const &buildEnvironment = phaseEnvironment.buildEnvironment();
    Target::Environment const &targetEnvironment = phaseEnvironment.targetEnvironment();

    Tool::CompilationInfo const &compilationInfo = phaseContext->toolContext().compilationInfo();

    std::vector<Tool::Invocation> invocations;

    std::unique_ptr<Tool::LinkerResolver> ldResolver = Tool::LinkerResolver::Create(phaseEnvironment, Tool::LinkerResolver::LinkerToolIdentifier());
    std::unique_ptr<Tool::LinkerResolver> libtoolResolver = Tool::LinkerResolver::Create(phaseEnvironment, Tool::LinkerResolver::LibtoolToolIdentifier());
    std::unique_ptr<Tool::LinkerResolver> lipoResolver = Tool::LinkerResolver::Create(phaseEnvironment, Tool::LinkerResolver::LipoToolIdentifier());
    std::unique_ptr<Tool::ToolResolver> dsymutil = Tool::ToolResolver::Create(phaseEnvironment, "com.apple.tools.dsymutil");
    if (ldResolver == nullptr || libtoolResolver == nullptr || lipoResolver == nullptr || dsymutil == nullptr) {
        fprintf(stderr, "error: couldn't get linker tools\n");
        return false;
    }

    std::string binaryType = targetEnvironment.environment().resolve("MACH_O_TYPE");

    Tool::LinkerResolver *linkerResolver = nullptr;
    std::string linkerExecutable;
    std::vector<std::string> linkerArguments;

    if (binaryType == "staticlib") {
        linkerResolver = libtoolResolver.get();
    } else {
        linkerResolver = ldResolver.get();
        linkerExecutable = compilationInfo.linkerDriver();
        linkerArguments.insert(linkerArguments.end(), compilationInfo.linkerArguments().begin(), compilationInfo.linkerArguments().end());
    }

    std::string workingDirectory = targetEnvironment.workingDirectory();
    std::string productsDirectory = targetEnvironment.environment().resolve("BUILT_PRODUCTS_DIR");

    std::vector<Phase::File> phaseFiles = Phase::File::ResolveBuildFiles(phaseEnvironment, targetEnvironment.environment(), _buildPhase->files());
    std::vector<TypeResolvedFile> files;
    for (Phase::File const &file : phaseFiles) {
        files.push_back(file.file());
    }

    for (std::string const &variant : targetEnvironment.variants()) {
        pbxsetting::Environment variantEnvironment = targetEnvironment.environment();
        variantEnvironment.insertFront(Phase::Environment::VariantLevel(variant), false);

        std::string variantIntermediatesName = variantEnvironment.resolve("EXECUTABLE_NAME") + variantEnvironment.resolve("EXECUTABLE_VARIANT_SUFFIX");
        std::string variantIntermediatesDirectory = variantEnvironment.resolve("OBJECT_FILE_DIR_" + variant);

        std::string variantProductsPath = variantEnvironment.resolve("EXECUTABLE_PATH") + variantEnvironment.resolve("EXECUTABLE_VARIANT_SUFFIX");
        std::string variantProductsOutput = productsDirectory + "/" + variantProductsPath;

        bool createUniversalBinary = targetEnvironment.architectures().size() > 1;
        std::vector<std::string> universalBinaryInputs;

        for (std::string const &arch : targetEnvironment.architectures()) {
            pbxsetting::Environment archEnvironment = variantEnvironment;
            archEnvironment.insertFront(Phase::Environment::ArchitectureLevel(arch), false);

            std::vector<std::string> sourceOutputs;
            auto it = phaseContext->toolContext().variantArchitectureInvocations().find(std::make_pair(variant, arch));
            if (it != phaseContext->toolContext().variantArchitectureInvocations().end()) {
                std::vector<Tool::Invocation const> const &sourceInvocations = it->second;
                for (Tool::Invocation const &invocation : sourceInvocations) {
                    for (std::string const &output : invocation.outputs()) {
                        // TODO(grp): Is this the right set of source outputs to link?
                        if (libutil::FSUtil::GetFileExtension(output) == "o") {
                            sourceOutputs.push_back(output);
                        }
                    }
                }
            }

            if (createUniversalBinary) {
                std::string architectureIntermediatesDirectory = variantIntermediatesDirectory + "/" + arch;
                std::string architectureIntermediatesOutput = architectureIntermediatesDirectory + "/" + variantIntermediatesName;

                linkerResolver->resolve(&phaseContext->toolContext(), archEnvironment, sourceOutputs, files, architectureIntermediatesOutput, linkerArguments, linkerExecutable);
                universalBinaryInputs.push_back(architectureIntermediatesOutput);
            } else {
                linkerResolver->resolve(&phaseContext->toolContext(), archEnvironment, sourceOutputs, files, variantProductsOutput, linkerArguments, linkerExecutable);
            }
        }

        if (createUniversalBinary) {
            lipoResolver->resolve(&phaseContext->toolContext(), variantEnvironment, universalBinaryInputs, { }, variantProductsOutput, { });
        }

        if (variantEnvironment.resolve("DEBUG_INFORMATION_FORMAT") == "dwarf-with-dsym" && (binaryType != "staticlib" && binaryType != "mh_object")) {
            std::string dsymfile = variantEnvironment.resolve("DWARF_DSYM_FOLDER_PATH") + "/" + variantEnvironment.resolve("DWARF_DSYM_FILE_NAME");
            dsymutil->resolve(&phaseContext->toolContext(), variantEnvironment, { variantProductsOutput }, { dsymfile });
        }
    }

    return true;
}
