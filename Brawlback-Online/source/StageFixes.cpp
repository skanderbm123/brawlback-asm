#include "StageFixes.h"
#include "Rollback_Hooks.h"
#include <modules.h>
#include <types.h>

namespace StageFixes {
    // Offsets into Brawl's stStadium class (mo_stage/st_stadium, module id
    // Modules::ST_STADIUM). m_event1 is a grTenganEvent at +0x288; its
    // m_state field (State enum: NoEvent=0, Running=1, ReadyEnd=2) is at
    // +0xa4 within that. stStadium::update() only queues the next terrain
    // transformation when m_event0.isReadyEnd() is true AND
    // !m_event1.isEvent() - forcing m_event1's state to Running makes that
    // second check permanently false, so the whole pick/shuffle block (and
    // everything downstream of it: background archive alloc/free, camera
    // events) never runs again for the rest of the match.
    constexpr u32 EVENT1_STATE_OFFSET = 0x288 + 0xA4;

    void FreezeStadiumTransform(void* stStadiumSelf) {
        if (!Netplay::IsInMatch()) return;
        u8* self = reinterpret_cast<u8*>(stStadiumSelf);
        *reinterpret_cast<u32*>(self + EVENT1_STATE_OFFSET) = 1; // grTenganEvent::Running
    }

    void InstallHooks(CoreApi* api) {
        // stStadium::update, right where it checks m_event0/m_event1 to
        // decide whether to queue the next terrain transformation.
        api->syInlineHookRel(0x000027A8, reinterpret_cast<void*>(FreezeStadiumTransform), Modules::ST_STADIUM);
    }
}
