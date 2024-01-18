#include "vip_chat.h"
#include "utils/module.h"
#include <stdio.h>
#include <subhook/subhook.h>
#include <unordered_map>

VIPChat g_VIPChat;

IVIPApi *g_pVIPCore;

IVEngineServer2 *engine = nullptr;
IServerGameDLL *server = nullptr;
CSchemaSystem *g_pCSchemaSystem = nullptr;
CGameEntitySystem *g_pGameEntitySystem = nullptr;
CEntitySystem *g_pEntitySystem = nullptr;

CGameEntitySystem *GameEntitySystem()
{
	return g_pVIPCore->VIP_GetEntitySystem();
}

// from https://github.com/xPaw/CS2
// C01 #ffffff  - Normal, white
// C02 #ff0000  - "Use old colors", dark red
// C03 #ba81f0  - Player name, team color (CT - #a2c6ff, T - #ffdf93)
// C04 #40ff40  - Location, bright green
// C05 #bfff90  - Achievement, light green
// C06 #a2ff47  - Award, bright green
// C07 #ff4040  - Penalty, light red
// C08 #c5cad0  - Silver
// C09 #ede47a (tab character) - Gold
// C10 #b0c3d9 (line feed) - Common?
// C11 #5e98d9  - Uncommon, very light blue
// C12 #4b69ff  - Rare, darker blue
// C13 #8847ff (carriage return) - Mythical?
// C14 #d32ce6  - Legendary, purple
// C15 #eb4b4b  - Ancient, lighter red
// C16 #e4ae39  - Immortal, oran

std::unordered_map<std::string, std::unordered_map<std::string, std::string>> g_TagColors;

std::unordered_map<std::string, char> g_Colors {
	{"none", '\0'},		   {"white", '\1'},		   {"darkred", '\2'},	  {"teamcolor", '\3'}, {"brightgreen", '\4'},
	{"lightgreen", '\5'},  {"brightgreen2", '\6'}, {"lightred", '\7'},	  {"silver", '\n'},	   {"gold", '\t'},
	{"common", '\10'},	   {"lightblue", '\11'},   {"darkerblue", '\12'}, {"mythical", '\13'}, {"purple", '\14'},
	{"lighterred", '\15'}, {"orange", '\16'}};

subhook::Hook *g_pUTIL_SayText2Filter_hook;

typedef void (*UTIL_SayText2Filter_t)(IRecipientFilter &, CBaseEntity *, uint64, const char *, const char *,
									  const char *, const char *, const char *);

void OnUTIL_SayText2Filter(IRecipientFilter &filter, CBaseEntity *pEntity, uint64 eMessageType, const char *msg_name,
						   const char *param1, const char *param2, const char *param3, const char *param4)
{
	CBasePlayerController *pController = static_cast<CBasePlayerController *>(pEntity);
	if(pController && engine->IsClientFullyAuthenticated(pController->GetPlayerSlot()) && pController->m_steamID() != 0)
	{
		const char *sChatTag = g_pVIPCore->VIP_GetClientFeatureString(pController->GetPlayerSlot(), "chat_tag");
		if(strlen(sChatTag) > 0 && g_TagColors.count(sChatTag))
		{
			// so bad, now teamcolor prints • with player color, if this dot not
			// needed we use none
			bool tagcolor = g_Colors.at(g_TagColors.at(sChatTag).at("chat_tag_color")) != '\0';
			bool namecolor = g_Colors.at(g_TagColors.at(sChatTag).at("chat_name_color")) != '\0';
			bool textcolor = g_Colors.at(g_TagColors.at(sChatTag).at("chat_text_color")) != '\0';
			char sTag[64], sName[64], sText[256], sBuffer[512];
			g_SMAPI->Format(sTag, sizeof(sTag), "%c%s",
							tagcolor ? g_Colors.at(g_TagColors.at(sChatTag).at("chat_tag_color")) : ' ', sChatTag);
			g_SMAPI->Format(sName, sizeof(sName), "%c%s",
							namecolor ? g_Colors.at(g_TagColors.at(sChatTag).at("chat_name_color")) : ' ', param1);
			g_SMAPI->Format(sText, sizeof(sText), "%c%s",
							textcolor ? g_Colors.at(g_TagColors.at(sChatTag).at("chat_text_color")) : ' ', param2);
			g_SMAPI->Format(sBuffer, sizeof(sBuffer), " %s %s: %s", tagcolor ? sTag : sTag + 1,
							namecolor ? sName : sName + 1, textcolor ? sText : sText + 1);
			((UTIL_SayText2Filter_t)g_pUTIL_SayText2Filter_hook->GetTrampoline())(filter, pEntity, eMessageType,
																				  sBuffer, "", "", "", "");
			return;
		}
	}

	((UTIL_SayText2Filter_t)g_pUTIL_SayText2Filter_hook->GetTrampoline())(filter, pEntity, eMessageType, msg_name,
																		  param1, param2, param3, param4);
}

PLUGIN_EXPOSE(VIPChat, g_VIPChat);
bool VIPChat::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();
	GET_V_IFACE_ANY(GetEngineFactory, g_pCSchemaSystem, CSchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetServerFactory, server, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);
	GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);

	g_SMAPI->AddListener(this, this);

	CModule libserver(server);
	CMemory UTIL_SayText2Filter =
		libserver.FindPatternSIMD(WIN_LINUX("48 89 5C 24 ? 55 56 57 48 8D 6C 24 ? 48 81 EC ? ? ? ? 41 0F B6 F8",
											"55 48 89 E5 41 57 4C 8D BD ? ? ? ? 41 56 4D 89 C6 41 55 4D 89 CD"));
	if(!UTIL_SayText2Filter)
	{
		g_SMAPI->Format(error, maxlen, "Failed to find function UTIL_SayText2Filter");
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);

		return false;
	}
	else
	{
		g_pUTIL_SayText2Filter_hook =
			new subhook::Hook((void *)UTIL_SayText2Filter, (void *)OnUTIL_SayText2Filter,
							  subhook::HookFlags::HookFlag64BitOffset | subhook::HookFlags::HookFlagTrampoline);
		if(!g_pUTIL_SayText2Filter_hook->Install())
		{
			g_SMAPI->Format(error, maxlen, "Failed to hook \"UTIL_SayText2Filter\" func");
			return false;
		}
	}

	char sPath[256];
	g_SMAPI->PathFormat(sPath, sizeof(sPath), "%s", "addons/configs/vip/vip_chat.ini");

	KeyValues *pKV = new KeyValues("VIPTagColors");
	if(!pKV->LoadFromFile(g_pFullFileSystem, sPath))
	{
		ConColorMsg(Color(255, 0, 0, 255), "[%s] Failed to load %s\n", GetLogTag(), sPath);
		return false;
	}

	for(KeyValues *pKey = pKV->GetFirstSubKey(); pKey; pKey = pKey->GetNextKey())
	{
		for(const auto &str : {"chat_tag_color", "chat_name_color", "chat_text_color"})
		{
			g_TagColors[pKey->GetName()][str] = pKey->GetString(str);
			if(!g_Colors.count(g_TagColors[pKey->GetName()][str]))
			{
				g_TagColors[pKey->GetName()][str] = "none";
			}
		}
	}

	delete pKV;

	return true;
}

bool VIPChat::Unload(char *error, size_t maxlen)
{
	g_pVIPCore = nullptr;
	return true;
}

void VIP_OnVIPLoaded()
{
	g_pGameEntitySystem = g_pVIPCore->VIP_GetEntitySystem();
	g_pEntitySystem = g_pGameEntitySystem;
}

void VIPChat::AllPluginsLoaded()
{
	int ret;
	g_pVIPCore = (IVIPApi *)g_SMAPI->MetaFactory(VIP_INTERFACE, &ret, NULL);

	if(ret == META_IFACE_FAILED)
	{
		ConColorMsg(Color(255, 0, 0, 255), "[%s] Failed to lookup vip core. Aborting\n", GetLogTag());
		std::string sCommand = "meta unload " + std::to_string(g_PLID);
		engine->ServerCommand(sCommand.c_str());
		return;
	}
	g_pVIPCore->VIP_OnVIPLoaded(VIP_OnVIPLoaded);
}

const char *VIPChat::GetLicense()
{
	return "Public";
}

const char *VIPChat::GetVersion()
{
	return "0.0.1.0";
}

const char *VIPChat::GetDate()
{
	return __DATE__;
}

const char *VIPChat::GetLogTag()
{
	return "[VIP-CHAT]";
}

const char *VIPChat::GetAuthor()
{
	return "idk";
}

const char *VIPChat::GetDescription()
{
	return "";
}

const char *VIPChat::GetName()
{
	return "[VIP] Chat";
}

const char *VIPChat::GetURL()
{
	return "";
}
