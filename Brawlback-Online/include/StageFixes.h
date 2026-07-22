#pragma once
#include "sy_core.h"

// Stage-specific rollback/netplay fixes. Pokemon Stadium's terrain
// transformation cycle is a heavy source of desyncs under rollback (Slippi
// hit the same problem on Melee's Pokemon Stadium and solved it the same
// way: make the transformation trigger permanently act as if frozen/in
// training mode, rather than trying to make the RNG pick + heap-allocated
// background archive lifecycle resimulate correctly).
namespace StageFixes {
    void InstallHooks(CoreApi* api);
    void FreezeStadiumTransform(void* stStadiumSelf);
}
