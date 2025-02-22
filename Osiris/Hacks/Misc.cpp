#include <sstream>

#include "../Config.h"
#include "../Interfaces.h"
#include "../Memory.h"
#include "../Netvars.h"
#include "Misc.h"
#include "../SDK/ConVar.h"
#include "../SDK/Surface.h"
#include "../SDK/GlobalVars.h"
#include "../SDK/NetworkChannel.h"
#include "../SDK/WeaponData.h"

void Misc::inverseRagdollGravity() noexcept
{
    static auto ragdollGravity = interfaces.cvar->findVar("cl_ragdoll_gravity");
    ragdollGravity->setValue(config.visuals.inverseRagdollGravity ? -600 : 600);
}

void Misc::updateClanTag(bool tagChanged) noexcept
{
    if (config.misc.customClanTag) {
        static std::string clanTag;

        if (tagChanged) {
            clanTag = config.misc.clanTag;
            if (!isblank(clanTag.front()) && !isblank(clanTag.back()))
                clanTag.push_back(' ');
        }

        static auto lastTime{ 0.0f };
        if (memory.globalVars->realtime - lastTime < 0.6f) return;
        lastTime = memory.globalVars->realtime;

        if (config.misc.animatedClanTag && !clanTag.empty())
            std::rotate(std::begin(clanTag), std::next(std::begin(clanTag)), std::end(clanTag));

        memory.setClanTag(clanTag.c_str(), clanTag.c_str());
    }
}

void Misc::spectatorList() noexcept
{
    if (config.misc.spectatorList && interfaces.engine->isInGame()) {
        const auto localPlayer = interfaces.entityList->getEntity(interfaces.engine->getLocalPlayer());

        if (!localPlayer->isAlive())
            return;

        interfaces.surface->setTextFont(Surface::font);
        interfaces.surface->setTextColor(51, 153, 255, 255);

        const auto [width, height] = interfaces.surface->getScreenSize();

        int textPositionY{ static_cast<int>(0.5f * height) };

        for (int i = 1; i <= interfaces.engine->getMaxClients(); i++) {
            auto entity = interfaces.entityList->getEntity(i);
            if (!entity || entity->isAlive() || entity->isDormant())
                continue;

            static PlayerInfo playerInfo;

            if (interfaces.engine->getPlayerInfo(i, playerInfo) && entity->getObserverTarget() == localPlayer) {
                static wchar_t name[128];
                if (MultiByteToWideChar(CP_UTF8, 0, playerInfo.name, -1, name, 128)) {
                    const auto [textWidth, textHeight] = interfaces.surface->getTextSize(Surface::font, name);
                    interfaces.surface->setTextPosition(width - textWidth - 5, textPositionY);
                    textPositionY -= textHeight;
                    interfaces.surface->printText(name);
                }
            }
        }
    }
}

void Misc::sniperCrosshair() noexcept
{
    static auto showSpread = interfaces.cvar->findVar("weapon_debug_spread_show");
    showSpread->setValue(config.misc.sniperCrosshair && !interfaces.entityList->getEntity(interfaces.engine->getLocalPlayer())->isScoped() ? 3 : 0);
}

void Misc::recoilCrosshair() noexcept
{
    static auto recoilCrosshair = interfaces.cvar->findVar("cl_crosshair_recoil");
    recoilCrosshair->setValue(config.misc.recoilCrosshair ? 1 : 0);
}

void Misc::watermark() noexcept
{
    if (config.misc.watermark) {
        interfaces.surface->setTextFont(Surface::font);
        interfaces.surface->setTextColor(sinf(0.6f * memory.globalVars->realtime) * 127 + 128,
            sinf(0.6f * memory.globalVars->realtime + 2.0f) * 127 + 128,
            sinf(0.6f * memory.globalVars->realtime + 4.0f) * 127 + 128,
            255.0f);

        interfaces.surface->setTextPosition(5, 0);
        interfaces.surface->printText(L"Osiris");

        static auto frameRate = 1.0f;
        frameRate = 0.9f * frameRate + 0.1f * memory.globalVars->absoluteFrameTime;
        const auto [screenWidth, screenHeight] = interfaces.surface->getScreenSize();
        std::wstring fps{ L"FPS: " + std::to_wstring(static_cast<int>(1 / frameRate)) };
        const auto [fpsWidth, fpsHeight] = interfaces.surface->getTextSize(Surface::font, fps.c_str());
        interfaces.surface->setTextPosition(screenWidth - fpsWidth - 5, 0);
        interfaces.surface->printText(fps.c_str());

        float latency = 0.0f;
        if (auto networkChannel = interfaces.engine->getNetworkChannel(); networkChannel && networkChannel->getLatency(0) > 0.0f)
            latency = networkChannel->getLatency(0);

        std::wstring ping{ L"PING: " + std::to_wstring(static_cast<int>(latency * 1000)) + L" ms" };
        const auto pingWidth = interfaces.surface->getTextSize(Surface::font, ping.c_str()).first;
        interfaces.surface->setTextPosition(screenWidth - pingWidth - 5, fpsHeight);
        interfaces.surface->printText(ping.c_str());
    }
}

void Misc::prepareRevolver(UserCmd* cmd) noexcept
{
    constexpr auto timeToTicks = [](float time) {  return static_cast<int>(0.5f + time / memory.globalVars->intervalPerTick); };
    constexpr float revolverPrepareTime{ 0.234375f };

    static float readyTime;
    if (config.misc.prepareRevolver && (!config.misc.prepareRevolverKey || GetAsyncKeyState(config.misc.prepareRevolverKey))) {
        const auto activeWeapon = interfaces.entityList->getEntity(interfaces.engine->getLocalPlayer())->getActiveWeapon();
        if (activeWeapon && activeWeapon->itemDefinitionIndex2() == WeaponId::Revolver) {
            if (!readyTime) readyTime = memory.globalVars->serverTime() + revolverPrepareTime;
            auto ticksToReady = timeToTicks(readyTime - memory.globalVars->serverTime() - interfaces.engine->getNetworkChannel()->getLatency(0));
            if (ticksToReady > 0 && ticksToReady <= timeToTicks(revolverPrepareTime))
                cmd->buttons |= UserCmd::IN_ATTACK;
            else
                readyTime = 0.0f;
        }
    }
}

void Misc::fastPlant(UserCmd* cmd) noexcept
{
    if (config.misc.fastPlant) {
        static auto plantAnywhere = interfaces.cvar->findVar("mp_plant_c4_anywhere");

        if (plantAnywhere->getInt()) return;

        const auto localPlayer = interfaces.entityList->getEntity(interfaces.engine->getLocalPlayer());
        if (!localPlayer->isAlive() || localPlayer->inBombZone()) return;

        const auto activeWeapon = localPlayer->getActiveWeapon();
        if (!activeWeapon || activeWeapon->getClientClass()->classId != ClassId::C4)
            return;

        cmd->buttons &= ~UserCmd::IN_ATTACK;

        constexpr float doorRange{ 200.0f };
        Vector viewAngles{ cos(degreesToRadians(cmd->viewangles.x)) * cos(degreesToRadians(cmd->viewangles.y)) * doorRange,
                           cos(degreesToRadians(cmd->viewangles.x)) * sin(degreesToRadians(cmd->viewangles.y)) * doorRange,
                          -sin(degreesToRadians(cmd->viewangles.x)) * doorRange };
        static Trace trace;
        interfaces.engineTrace->traceRay({ localPlayer->getEyePosition(), localPlayer->getEyePosition() + viewAngles }, 0x46004009, localPlayer, trace);

        if (!trace.entity || trace.entity->getClientClass()->classId != ClassId::PropDoorRotating)
            cmd->buttons &= ~UserCmd::IN_USE;
    }
}

void Misc::drawBombTimer() noexcept
{
    if (config.misc.bombTimer) {
        for (int i = interfaces.engine->getMaxClients(); i <= interfaces.entityList->getHighestEntityIndex(); i++) {
            Entity* entity = interfaces.entityList->getEntity(i);
            if (!entity || entity->isDormant() || entity->getClientClass()->classId != ClassId::PlantedC4 || !entity->c4Ticking())
                continue;

            static constexpr unsigned font{ 0xc1 };
            interfaces.surface->setTextFont(font);
            interfaces.surface->setTextColor(255.0f, 255.0f, 255.0f, 255.0f);
            auto drawPositionY{ interfaces.surface->getScreenSize().second / 8 };
            auto bombText{ (std::wstringstream{ } << L"Bomb on " << (!entity->c4BombSite() ? 'A' : 'B') << L" : " << std::showpoint << std::setprecision(3) << (std::max)(entity->c4BlowTime() - memory.globalVars->currenttime, 0.0f) << L" s").str() };
            const auto bombTextX{ interfaces.surface->getScreenSize().first / 2 - static_cast<int>((interfaces.surface->getTextSize(font, bombText.c_str())).first / 2) };
            interfaces.surface->setTextPosition(bombTextX, drawPositionY);
            drawPositionY += interfaces.surface->getTextSize(font, bombText.c_str()).second;
            interfaces.surface->printText(bombText.c_str());

            const auto progressBarX{ interfaces.surface->getScreenSize().first / 3 };
            const auto progressBarLength{ interfaces.surface->getScreenSize().first / 3 };
            constexpr auto progressBarHeight{ 5 };

            interfaces.surface->setDrawColor(50, 50, 50, 255);
            interfaces.surface->drawFilledRect(progressBarX - 3, drawPositionY + 2, progressBarX + progressBarLength + 3, drawPositionY + progressBarHeight + 8);
            interfaces.surface->setDrawColor(255, 140, 0, 255);
            interfaces.surface->drawFilledRect(progressBarX, drawPositionY + 5, static_cast<int>(progressBarX + progressBarLength * (std::max)(entity->c4BlowTime() - memory.globalVars->currenttime, 0.0f) / 40.0f), drawPositionY + progressBarHeight + 5);

            if (entity->c4Defuser() != -1) {
                static PlayerInfo playerInfo;
                if (interfaces.engine->getPlayerInfo(interfaces.entityList->getEntityFromHandle(entity->c4Defuser())->index(), playerInfo)) {
                    static wchar_t name[128];
                    if (MultiByteToWideChar(CP_UTF8, 0, playerInfo.name, -1, name, 128)) {
                        drawPositionY += interfaces.surface->getTextSize(font, L" ").second;
                        const auto defusingText{ (std::wstringstream{ } << name << L" is defusing: " << std::showpoint << std::setprecision(4) << (std::max)(entity->c4DefuseCountDown() - memory.globalVars->currenttime, 0.0f) << L" s").str() };

                        interfaces.surface->setTextPosition((interfaces.surface->getScreenSize().first - interfaces.surface->getTextSize(font, defusingText.c_str()).first) / 2, drawPositionY);
                        interfaces.surface->printText(defusingText.c_str());
                        drawPositionY += interfaces.surface->getTextSize(font, L" ").second;

                        interfaces.surface->setDrawColor(50, 50, 50, 255);
                        interfaces.surface->drawFilledRect(progressBarX - 3, drawPositionY + 2, progressBarX + progressBarLength + 3, drawPositionY + progressBarHeight + 8);
                        interfaces.surface->setDrawColor(0, 255, 0, 255);
                        interfaces.surface->drawFilledRect(progressBarX, drawPositionY + 5, progressBarX + static_cast<int>(progressBarLength * (std::max)(entity->c4DefuseCountDown() - memory.globalVars->currenttime, 0.0f) / (interfaces.entityList->getEntityFromHandle(entity->c4Defuser())->hasDefuser() ? 5.0f : 10.0f)), drawPositionY + progressBarHeight + 5);

                        drawPositionY += interfaces.surface->getTextSize(font, L" ").second;
                        const wchar_t* canDefuseText;

                        if (entity->c4BlowTime() >= entity->c4DefuseCountDown()) {
                            canDefuseText = L"Can Defuse";
                            interfaces.surface->setTextColor(0.0f, 255.0f, 0.0f, 255.0f);
                        } else {
                            canDefuseText = L"Cannot Defuse";
                            interfaces.surface->setTextColor(255.0f, 0.0f, 0.0f, 255.0f);
                        }

                        interfaces.surface->setTextPosition((interfaces.surface->getScreenSize().first - interfaces.surface->getTextSize(font, canDefuseText).first) / 2, drawPositionY);
                        interfaces.surface->printText(canDefuseText);
                    }
                }
            }
        }
    }
}

void Misc::stealNames() noexcept
{
    if (config.misc.nameStealer) {
        const auto localPlayer = interfaces.entityList->getEntity(interfaces.engine->getLocalPlayer());

        bool allNamesStolen = true;
        static std::vector<int> stolenIds;
        for (int i = 1; i <= interfaces.engine->getMaxClients(); i++) {
            if (auto entity = interfaces.entityList->getEntity(i); entity && entity != localPlayer) {
                static PlayerInfo playerInfo;
                if (interfaces.engine->getPlayerInfo(entity->index(), playerInfo) && !playerInfo.fakeplayer && std::find(std::begin(stolenIds), std::end(stolenIds), playerInfo.userId) == std::end(stolenIds)) {
                    allNamesStolen = false;
                    if (changeName(false, std::string{ playerInfo.name }.append("\x1").c_str(), 1.0f))
                        stolenIds.push_back(playerInfo.userId);
                    break;
                }
            }
        }
        if (allNamesStolen)
            stolenIds.clear();
    }
}

void Misc::quickReload(UserCmd* cmd) noexcept
{
    if (config.misc.quickReload) {
        const auto localPlayer = interfaces.entityList->getEntity(interfaces.engine->getLocalPlayer());
        static Entity* reloadedWeapon{ nullptr };

        if (reloadedWeapon) {
            cmd->weaponselect = reloadedWeapon->index();
            cmd->weaponsubtype = reloadedWeapon->getWeaponSubType();
            reloadedWeapon = nullptr;
        }

        if (auto activeWeapon{ localPlayer->getActiveWeapon() }; activeWeapon && activeWeapon->isInReload() && activeWeapon->clip() == activeWeapon->getWeaponData()->maxClip) {
            reloadedWeapon = activeWeapon;

            for (auto weaponHandle : localPlayer->weapons()) {
                if (weaponHandle == -1)
                    break;

                if (auto weapon{ interfaces.entityList->getEntityFromHandle(weaponHandle) }; weapon && weapon != reloadedWeapon) {
                    cmd->weaponselect = weapon->index();
                    cmd->weaponsubtype = weapon->getWeaponSubType();
                    break;
                }
            }

        }
    }
}

bool Misc::changeName(bool reconnect, const char* newName, float delay) noexcept
{
    static auto exploitInitialized{ false };

    static auto name{ interfaces.cvar->findVar("name") };

    if (reconnect) {
        exploitInitialized = false;
        return false;
    }

    if (!exploitInitialized && interfaces.engine->isInGame()) {
        if (PlayerInfo playerInfo; interfaces.engine->getPlayerInfo(interfaces.engine->getLocalPlayer(), playerInfo) && (!strcmp(playerInfo.name, "?empty") || !strcmp(playerInfo.name, "\n\xAD\xAD\xAD"))) {
            exploitInitialized = true;
        } else {
            name->onChangeCallbacks.size = 0;
            name->setValue("\n\xAD\xAD\xAD");
            return false;
        }
    }

    static auto nextChangeTime{ 0.0f };
    if (nextChangeTime <= memory.globalVars->realtime) {
        name->setValue(newName);
        nextChangeTime = memory.globalVars->realtime + delay;
        return true;
    }
    return false;
}

void Misc::fakeVote(bool set) noexcept
{
    static bool shouldSet = false;

    if (set)
        shouldSet = set;

    if (shouldSet && interfaces.engine->isInGame() && changeName(false, std::string(25, '\n').append(config.misc.voteText).append(50, '\n').c_str(), 10.0f))
        shouldSet = false;
}

void Misc::bunnyHop(UserCmd* cmd) noexcept
{
    const auto localPlayer{ interfaces.entityList->getEntity(interfaces.engine->getLocalPlayer()) };

    static auto wasLastTimeOnGround{ localPlayer->flags() & 1 };

    if (config.misc.bunnyHop && !(localPlayer->flags() & 1) && localPlayer->moveType() != MoveType::LADDER && !wasLastTimeOnGround)
        cmd->buttons &= ~UserCmd::IN_JUMP;

    wasLastTimeOnGround = localPlayer->flags() & 1;
}

void Misc::fakeBan(bool set) noexcept
{
    static bool shouldSet = false;

    if (set)
        shouldSet = set;

    if (shouldSet && interfaces.engine->isInGame() && changeName(false, std::string{ static_cast<char>(config.misc.banColor + 1) }.append(config.misc.banText).append("\x1").c_str(), 5.0f))
        shouldSet = false;
}

void Misc::nadePredict() noexcept
{ 
    static auto nadeVar{ interfaces.cvar->findVar("cl_grenadepreview") }; 
    
    nadeVar->onChangeCallbacks.size = 0; 
    nadeVar->setValue(config.misc.nadePredict); 
}
