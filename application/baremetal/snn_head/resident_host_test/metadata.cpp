#include "artifact_reader.h"
#include "generated/compile_artifacts_generated.h"

extern "C" uint32_t resident_test_thread_id(const rvrt_artifact_t *artifact)
{
    namespace fbs = paibox::backendv2::generated::fbs;
    const auto *io = static_cast<const fbs::IOMapping *>(artifact->io_mapping);
    return io->threads()->Get(0)->thread_id();
}
