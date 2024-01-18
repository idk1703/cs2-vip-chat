#pragma once
#include "CBaseEntity.h"
#include "CBasePlayerPawn.h"
#include "ehandle.h"
#include "schemasystem.h"

class CBasePlayerController : public SC_CBaseEntity
{
public:
    SCHEMA_FIELD(uint64_t, CBasePlayerController, m_steamID);

    int GetPlayerSlot() { return m_pEntity->m_EHandle.GetEntryIndex() - 1; }
};