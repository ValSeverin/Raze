//-------------------------------------------------------------------------
/*
Copyright (C) 2010-2019 EDuke32 developers and contributors
Copyright (C) 2019 sirlemonhead, Nuke.YKT
This file is part of PCExhumed.
PCExhumed is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
//-------------------------------------------------------------------------
#include "ns.h"
#include "engine.h"
#include "exhumed.h"
#include "aistuff.h"
#include "player.h"
#include "view.h"
#include "status.h"
#include "sound.h"
#include "mapinfo.h"
#include <string.h>
#include <assert.h>


BEGIN_PS_NS

int nPushBlocks;

// TODO - moveme?
sectortype* overridesect;

enum
{
    kMaxPushBlocks = 100,
    kMaxMoveChunks = 75
};


TObjPtr<DExhumedActor*> nBodySprite[50];
TObjPtr<DExhumedActor*> nChunkSprite[kMaxMoveChunks];
BlockInfo sBlockInfo[kMaxPushBlocks];
TObjPtr<DExhumedActor*> nBodyGunSprite[50];
int nCurBodyGunNum;

Collision loHit, hiHit;

// think this belongs in init.c?


size_t MarkMove()
{
    GC::MarkArray(nBodySprite, 50);
    GC::MarkArray(nChunkSprite, kMaxMoveChunks);
    for(int i = 0; i < nPushBlocks; i++)
        GC::Mark(sBlockInfo[i].pActor);
    return 50 + kMaxMoveChunks + nPushBlocks;
}

FSerializer& Serialize(FSerializer& arc, const char* keyname, BlockInfo& w, BlockInfo* def)
{
    if (arc.BeginObject(keyname))
    {
        arc("mindist", w.mindist)
            ("sprite", w.pActor)
            ("pos", w.pos)
            .EndObject();
    }
    return arc;
}

void SerializeMove(FSerializer& arc)
{
    if (arc.BeginObject("move"))
    {
        arc ("pushcount", nPushBlocks)
            .Array("blocks", sBlockInfo, nPushBlocks)
            ("chunkcount", nCurChunkNum)
            .Array("chunks", nChunkSprite, kMaxMoveChunks)
            ("overridesect", overridesect)
            .Array("bodysprite", nBodySprite, countof(nBodySprite))
            ("curbodygun", nCurBodyGunNum)
            .Array("bodygunsprite", nBodyGunSprite, countof(nBodyGunSprite))
            .EndObject();
    }
}

void MoveThings()
{
    thinktime.Reset();
    thinktime.Clock();

    UndoFlashes();
    DoLights();

    if (nFreeze)
    {
        if (nFreeze == 1 || nFreeze == 2) {
            DoSpiritHead();
        }
    }
    else
    {
        actortime.Reset();
        actortime.Clock();
        runlist_ExecObjects();
        runlist_CleanRunRecs();
        actortime.Unclock();
    }

    DoBubbleMachines();
    DoDrips();
    DoMovingSects();
    DoRegenerates();

    if (currentLevel->gameflags & LEVEL_EX_COUNTDOWN)
    {
        DoFinale();
        if (lCountDown < 1800 && nDronePitch < 2400 && !lFinaleStart)
        {
            nDronePitch += 64;
            BendAmbientSound();
        }
    }

    thinktime.Unclock();
}

void ResetMoveFifo()
{
    movefifoend = 0;
    movefifopos = 0;
}

// not used
void clipwall()
{

}

int BelowNear(DExhumedActor* pActor, double walldist)
{
    auto pSector = pActor->sector();
    double z = pActor->spr.pos.Z;

    double z2;

    if (loHit.type == kHitSprite)
    {
        z2 = loHit.actor()->spr.pos.Z;
    }
    else
    {
        z2 = pSector->floorz + pSector->Depth;

        BFSSectorSearch search(pSector);

        sectortype* pTempSect = nullptr;
        while (auto pCurSector = search.GetNext())
        {
            for (auto& wal : wallsofsector(pCurSector))
            {
                if (wal.twoSided())
                {
                    if (!search.Check(wal.nextSector()))
                    {
                        if (IsCloseToWall(pActor->spr.pos, &wal, walldist) != EClose::Outside)
                        {
                            search.Add(wal.nextSector());
                        }
                    }
                }
            }

            auto pSect2 = pCurSector;

            while (pSect2)
            {
                pTempSect = pSect2;
                pSect2 = pSect2->pBelow;
            }

            double lowestZ = pTempSect->floorz + pTempSect->Depth;
            double lowestDiff = lowestZ - z;

            if (lowestDiff < 0 && lowestDiff >= -20)
            {
                z2 = lowestZ;
                pSector = pTempSect;
            }
        }
    }


    if (z2 < pActor->spr.pos.Z)
    {
        pActor->spr.pos.Z = z2;
        overridesect = pSector;
        pActor->vel.Z = 0;

        bTouchFloor = true;

        return kHitAux2;
    }
    else
    {
        return 0;
    }
}

Collision movespritez(DExhumedActor* pActor, double z, double height, int clipdist)
{
    auto pSector = pActor->sector();
    assert(pSector);

    overridesect = pSector;
    auto pSect2 = pSector;

    // backup cstat
    auto cstat = pActor->spr.cstat;

    pActor->spr.cstat &= ~CSTAT_SPRITE_BLOCK;

    Collision nRet;
    nRet.setNone();

    int nSectFlags = pSector->Flag;

    if (nSectFlags & kSectUnderwater) {
        z *= 0.5;
    }

    double spriteZ = pActor->spr.pos.Z;
    double floorZ = pSector->floorz;

    double destZ = spriteZ + z;
    double highestZ = pSector->ceilingz + (height * 0.5);

    if ((nSectFlags & kSectUnderwater) && destZ < highestZ) {
        destZ = highestZ;
    }

    // loc_151E7:
    while (destZ > pActor->sector()->floorz && pActor->sector()->pBelow != nullptr)
    {
        ChangeActorSect(pActor, pActor->sector()->pBelow);
    }

    if (pSect2 != pSector)
    {
        pActor->spr.pos.Z = destZ;

        if (pSect2->Flag & kSectUnderwater)
        {
            if (pActor == PlayerList[nLocalPlayer].pActor) {
                D3PlayFX(StaticSound[kSound2], pActor);
            }

            if (pActor->spr.statnum <= 107) {
                pActor->spr.hitag = 0;
            }
        }
    }
    else
    {
        while ((destZ < pActor->sector()->ceilingz) && (pActor->sector()->pAbove != nullptr))
        {
            ChangeActorSect(pActor, pActor->sector()->pAbove);
        }
    }

    // This function will keep the player from falling off cliffs when you're too close to the edge.
    // This function finds the highest and lowest z coordinates that your clipping BOX can get to.
    double sprceiling, sprfloor;

    auto pos = pActor->spr.pos.plusZ(-1);
    getzrange(pos, pActor->sector(), &sprceiling, hiHit, &sprfloor, loHit, 128, CLIPMASK0);

    double mySprfloor = sprfloor;

    if (loHit.type != kHitSprite) {
        mySprfloor += pActor->sector()->Depth;
    }

    if (destZ > mySprfloor)
    {
        if (z > 0)
        {
            bTouchFloor = true;

            if (loHit.type == kHitSprite)
            {
                // Path A
                auto pFloorActor = loHit.actor();

                if (pActor->spr.statnum == 100 && pFloorActor->spr.statnum != 0 && pFloorActor->spr.statnum < 100)
                {
                    int nDamage = int(z * 0.5);
                    if (nDamage)
                    {
                        runlist_DamageEnemy(loHit.actor(), pActor, nDamage << 1);
                    }

                    pActor->vel.Z = -z;
                }
                else
                {
                    if (pFloorActor->spr.statnum == 0 || pFloorActor->spr.statnum > 199)
                    {
                        nRet.exbits |= kHitAux2;
                    }
                    else
                    {
                        nRet = loHit;
                    }

                    pActor->vel.Z = 0;
                }
            }
            else
            {
                // Path B
                if (pActor->sector()->pBelow == nullptr)
                {
                    nRet.exbits |= kHitAux2;

                    int nSectDamage = pActor->sector()->Damage;

                    if (nSectDamage != 0)
                    {
                        if (pActor->spr.hitag < 15)
                        {
                            IgniteSprite(pActor);
                            pActor->spr.hitag = 20;
                        }
                        nSectDamage >>= 2;
                        nSectDamage = nSectDamage - (nSectDamage>>2);
                        if (nSectDamage) {
                            runlist_DamageEnemy(pActor, nullptr, nSectDamage);
                        }
                    }

                    pActor->vel.Z = 0;
                }
            }
        }

        // loc_1543B:
        destZ = mySprfloor;
        pActor->spr.pos.Z = mySprfloor;
    }
    else
    {
        if ((destZ - height) < sprceiling && (hiHit.type == kHitSprite || pActor->sector()->pAbove == nullptr))
        {
            destZ = sprceiling + height;
            nRet.exbits |= kHitAux1;
        }
    }

    if (spriteZ <= floorZ && destZ > floorZ)
    {
        if ((pSector->Depth != 0) || (pSect2 != pSector && (pSect2->Flag & kSectUnderwater)))
        {
            BuildSplash(pActor, pSector);
        }
    }

    pActor->spr.cstat = cstat; // restore cstat
    pActor->spr.pos.Z = destZ;

    if (pActor->spr.statnum == 100)
    {
        nRet.exbits |= BelowNear(pActor, clipdist * (inttoworld * 1.5));
    }

    return nRet;
}

int GetActorHeight(DExhumedActor* actor)
{
    return tileHeight(actor->spr.picnum) * actor->spr.yrepeat * 4;
}

DExhumedActor* insertActor(sectortype* s, int st)
{
    return static_cast<DExhumedActor*>(::InsertActor(RUNTIME_CLASS(DExhumedActor), s, st));
}


Collision movesprite(DExhumedActor* pActor, int dx, int dy, int dz, int ceildist, int flordist, unsigned int clipmask)
{
    DVector2 vect(FixedToFloat<18>(dx), FixedToFloat<18>(dy));

    bTouchFloor = false;

	auto spos = pActor->spr.pos;
    double nSpriteHeight = GetActorHeightF(pActor);
    int nClipDist = pActor->int_clipdist();
    auto pSector = pActor->sector();
    assert(pSector);

	double floorZ = pSector->floorz;

    if ((pSector->Flag & kSectUnderwater) || (floorZ < spos.Z))
    {
        vect *= 0.5;
    }

    Collision nRet = movespritez(pActor, dz * zinttoworld, nSpriteHeight, nClipDist);

    pSector = pActor->sector(); // modified in movespritez so re-grab this variable

    if (pActor->spr.statnum == 100)
    {
        int nPlayer = GetPlayerFromActor(pActor);
        DVector2 thrust(0, 0);

        CheckSectorFloor(overridesect, pActor->spr.pos.Z, thrust);
        if (!thrust.isZero())
        {
            PlayerList[nPlayer].nThrust = thrust;
        }

        vect += PlayerList[nPlayer].nThrust;
    }
    else
    {
        CheckSectorFloor(overridesect, pActor->spr.pos.Z, vect);
    }

    Collision coll;
    clipmove(pActor->spr.pos, &pSector, FloatToFixed<18>(vect.X), FloatToFixed<18>(vect.Y), nClipDist, int(nSpriteHeight * zworldtoint), flordist, clipmask, coll);
    if (coll.type != kHitNone) // originally this or'ed the two values which can create unpredictable bad values in some edge cases.
    {
        coll.exbits = nRet.exbits;
        nRet = coll;
    }

    if ((pSector != pActor->sector()) && pSector != nullptr)
    {
        if (nRet.exbits & kHitAux2) {
            dz = 0;
        }

        if ((pSector->floorz - spos.Z) < (dz + flordist) * zinttoworld)
        {
			pActor->spr.pos.XY() = spos.XY();
        }
        else
        {
            ChangeActorSect(pActor, pSector);

            if (pActor->spr.pal < 5 && !pActor->spr.hitag)
            {
                pActor->spr.pal = pActor->sector()->ceilingpal;
            }
        }
    }

    return nRet;
}

void Gravity(DExhumedActor* pActor)
{
    if (pActor->sector()->Flag & kSectUnderwater)
    {
        if (pActor->spr.statnum != 100)
        {
            if (pActor->vel.Z <= 4)
            {
                if (pActor->vel.Z < 8) {
                    pActor->vel.Z += 2;
                }
            }
            else
            {
				pActor->vel.Z -= 0.25;
            }
        }
        else
        {
            if (pActor->vel.Z > 0)
            {
				pActor->vel.Z -= 0.25;
                if (pActor->vel.Z < 0) {
                    pActor->vel.Z = 0;
                }
            }
            else if (pActor->vel.Z < 0)
            {
				pActor->vel.Z += 0.25;
                if (pActor->vel.Z > 0) {
                    pActor->vel.Z = 0;
                }
            }
        }
    }
    else
    {
		pActor->vel.Z += 2;
        if (pActor->vel.Z > 64) {
            pActor-> vel.Z = 64;
        }
    }
}

Collision MoveCreature(DExhumedActor* pActor)
{
    return movesprite(pActor, pActor->vel, 256., 15360, -5120, CLIPMASK0);
}

Collision MoveCreatureWithCaution(DExhumedActor* pActor)
{
	auto oldv = pActor->spr.pos;
    auto pSectorPre = pActor->sector();

    auto ecx = MoveCreature(pActor);

    auto pSector =pActor->sector();

    if (pSector != pSectorPre)
    {
        int zDiff = pSectorPre->int_floorz() - pSector->int_floorz();
        if (zDiff < 0) {
            zDiff = -zDiff;
        }

        if (zDiff > 15360 || (pSector->Flag & kSectUnderwater) || (pSector->pBelow != nullptr && pSector->pBelow->Flag) || pSector->Damage)
        {
			pActor->spr.pos = oldv;

            ChangeActorSect(pActor, pSectorPre);

            pActor->spr.angle += DAngle45;
            pActor->VelFromAngle(-2);
            Collision c;
            c.setNone();
            return c;
        }
    }

    return ecx;
}

int GetAngleToSprite(DExhumedActor* a1, DExhumedActor* a2)
{
    if (!a1 || !a2)
        return -1;

    return getangle(a2->spr.pos - a1->spr.pos);
}

int PlotCourseToSprite(DExhumedActor* pActor1, DExhumedActor* pActor2)
{
    if (pActor1 == nullptr || pActor2 == nullptr)
        return -1;
	
	auto vect = pActor2->spr.pos.XY() - pActor1->spr.pos.XY();
	pActor1->spr.angle = VecToAngle(vect);
	return int(vect.Length() * worldtoint);

}

DExhumedActor* FindPlayer(DExhumedActor* pActor, int nDistance, bool dontengage)
{
    int var_18 = !dontengage;

    if (nDistance < 0)
        nDistance = 100;

	auto pSector =pActor->sector();
    nDistance <<= 4;

    DExhumedActor* pPlayerActor = nullptr;
    int i = 0;

    while (1)
    {
        if (i >= nTotalPlayers)
            return nullptr;

        pPlayerActor = PlayerList[i].pActor;

        if ((pPlayerActor->spr.cstat & CSTAT_SPRITE_BLOCK_ALL) && (!(pPlayerActor->spr.cstat & CSTAT_SPRITE_INVISIBLE)))
        {
            int v9 = abs(pPlayerActor->spr.pos.X - pActor->spr.pos.X);

            if (v9 < nDistance)
            {
                int v10 = abs(pPlayerActor->spr.pos.Y - pActor->spr.pos.Y);

                if (v10 < nDistance && cansee(pPlayerActor->spr.pos.plusZ(-30), pPlayerActor->sector(), pActor->spr.pos.plusZ(-GetActorHeightF(pActor)), pSector))
                {
                    break;
                }
            }
        }

        i++;
    }

    if (var_18) {
        PlotCourseToSprite(pActor, pPlayerActor);
    }

    return pPlayerActor;
}

void CheckSectorFloor(sectortype* pSector, double z, DVector2& xy)
{
    int nSpeed = pSector->Speed;

    if (!nSpeed) {
        return;
    }

    DAngle nAng = DAngle::fromBuild(pSector->Flag & kAngleMask);

    if (z >= pSector->floorz)
    {
        xy += nAng.ToVector() * nSpeed * 0.5;
    }
    else if (pSector->Flag & 0x800)
    {
        xy += nAng.ToVector() * nSpeed;
    }
}

void InitPushBlocks()
{
    nPushBlocks = 0;
    memset(sBlockInfo, 0, sizeof(sBlockInfo));
}

int GrabPushBlock()
{
    if (nPushBlocks >= kMaxPushBlocks) {
        return -1;
    }

    return nPushBlocks++;
}

void CreatePushBlock(sectortype* pSector)
{
    int nBlock = GrabPushBlock();
    DVector2 sum(0, 0);

    for (auto& wal : wallsofsector(pSector))
    {
        sum += wal.pos;
    }

    DVector2 avg = sum / pSector->wallnum;

    sBlockInfo[nBlock].pos = avg;

    auto pActor = insertActor(pSector, 0);

    sBlockInfo[nBlock].pActor = pActor;

    pActor->spr.pos = { avg, pSector->floorz- 1 };
    pActor->spr.cstat = CSTAT_SPRITE_INVISIBLE;

    double mindist = 0;

	for (auto& wal : wallsofsector(pSector))
    {
        double length = (avg - wal.pos).Length();

        if (length > mindist) {
            mindist = length;
        }
    }

    sBlockInfo[nBlock].mindist = mindist;

    pActor->set_native_clipdist( (int(mindist * worldtoint) & 0xFF) << 2);
    pSector->extra = nBlock;
}


void MoveSector(sectortype* pSector, DAngle nAngle, int *nXVel, int *nYVel)
{
    if (pSector == nullptr) {
        return;
    }

    DVector2 nVect;

    if (nAngle < nullAngle)
    {
        nVect = { FixedToFloat<18>(*nXVel), FixedToFloat<18>(*nYVel) };
        nAngle = VecToAngle(nVect);
    }
    else
    {
        nVect = nAngle.ToVector() * 4;
    }

    int nBlock = pSector->extra;
    int nSectFlag = pSector->Flag;

    double nFloorZ = pSector->floorz;

    walltype *pStartWall = pSector->firstWall();
    sectortype* pNextSector = pStartWall->nextSector();

    BlockInfo *pBlockInfo = &sBlockInfo[nBlock];

    DVector3 pos;

    pos.XY() = sBlockInfo[nBlock].pos;
    auto b_pos = pos.XY();

    double nZVal;

    int bUnderwater = nSectFlag & kSectUnderwater;

    if (nSectFlag & kSectUnderwater)
    {
        nZVal = pSector->ceilingz;
        pos.Z = pNextSector->ceilingz + 1;

        pSector->setceilingz(pNextSector->ceilingz);
    }
    else
    {
        nZVal = pSector->floorz;
        pos.Z = pNextSector->floorz - 1;

        pSector->setfloorz(pNextSector->floorz);
    }

    auto pSectorB = pSector;
    Collision scratch;
    clipmove(pos, &pSectorB, FloatToFixed<18>(nVect.X), FloatToFixed<18>(nVect.Y), pBlockInfo->mindist, 0, 0, CLIPMASK1, scratch);

    auto vect = pos.XY() - b_pos;

    if (pSectorB != pNextSector && pSectorB != pSector)
    {
        vect.Zero();
    }
    else
    {
        if (!bUnderwater)
        {
            pos.XY() = b_pos;
            pos.Z = nZVal;

            clipmove(pos, &pSectorB, FloatToFixed<18>(nVect.X), FloatToFixed<18>(nVect.Y), pBlockInfo->mindist, 0, 0, CLIPMASK1, scratch);

            auto delta = pos.XY() - b_pos;

            if (abs(vect.X) > abs(delta.X))
            {
                vect.X = delta.X;
            }

            if (abs(vect.Y) > abs(delta.Y)) 
            {
                vect.Y = delta.Y;
            }
        }
    }

    // GREEN
    if (!vect.isZero())
    {
        ExhumedSectIterator it(pSector);
        while (auto pActor = it.Next())
        {
            if (pActor->spr.statnum < 99)
            {
                pActor->spr.pos += vect;
            }
            else
            {
                pos.Z = pActor->spr.pos.Z;

                if ((nSectFlag & kSectUnderwater) || pos.Z != nZVal || pActor->spr.cstat & CSTAT_SPRITE_INVISIBLE)
                {
                    pos.XY() = pActor->spr.pos.XY();
                    pSectorB = pSector;

                    // The vector that got passed in here originally was Q28.4, while clipmove expects Q14.18, effectively resulting in actual zero movement
                    // because the resulting offset would be far below the coordinate's precision.
                    clipmove(pos, &pSectorB, FloatToFixed<18>(-vect.X / 16384.), FloatToFixed<18>(-vect.Y / 16384), pActor->int_clipdist(), 0, 0, CLIPMASK0, scratch);

                    if (pSectorB) {
                        ChangeActorSect(pActor, pSectorB);
                    }
                }
            }
        }
        it.Reset(pNextSector);
        while (auto pActor = it.Next())
        {
            if (pActor->spr.statnum >= 99)
            {
                pos = pActor->spr.pos;
                pSectorB = pNextSector;

                // Original used 14 bits of scale from the sine table and 4 bits from clipdist.
                // vect was added unscaled, essentially nullifying its effect entirely.
                auto vect2 = -nAngle.ToVector() * pActor->fClipdist()/* - vect*/;

                clipmove(pos, &pSectorB, FloatToFixed<18>(vect2.X), FloatToFixed<18>(vect2.Y), pActor->int_clipdist(), 0, 0, CLIPMASK0, scratch);


                if (pSectorB != pNextSector && (pSectorB == pSector || pNextSector == pSector))
                {
                    if (pSectorB != pSector || nFloorZ >= pActor->spr.pos.Z)
                    {
                        if (pSectorB) {
                            ChangeActorSect(pActor, pSectorB);
                        }
                    }
                    else
                    {
                        // Unlike the above, this one *did* scale vect
                        vect2 = nAngle.ToVector() * pActor->fClipdist() * 0.25 + vect;
                        movesprite(pActor, FloatToFixed<18>(vect2.X), FloatToFixed<18>(vect2.Y), 0, 0, 0, CLIPMASK0);
                    }
                }
            }
        }

		for(auto& wal : wallsofsector(pSector))
        {
            dragpoint(&wal, vect + wal.pos);
        }

        pBlockInfo->pos += vect;
    }

    // loc_163DD

    if (!(nSectFlag & kSectUnderwater))
    {
        ExhumedSectIterator it(pSector);
        while (auto pActor = it.Next())
        {
            if (pActor->spr.statnum >= 99 && nZVal == pActor->spr.pos.Z && !(pActor->spr.cstat & CSTAT_SPRITE_INVISIBLE))
            {
                pSectorB = pSector;
                clipmove(pActor->spr.pos, &pSectorB, FloatToFixed<18>(vect.X), FloatToFixed<18>(vect.Y), pActor->int_clipdist(), 5120, -5120, CLIPMASK0, scratch);
            }
        }
    }

    if (nSectFlag & kSectUnderwater) {
        pSector->setceilingz(nZVal);
    }
    else {
        pSector->setfloorz(nZVal);
    }

    *nXVel = FloatToFixed<18>(vect.X);
    *nYVel = FloatToFixed<18>(vect.Y);

    /* 
        Update player position variables, in case the player sprite was moved by a sector,
        Otherwise these can be out of sync when used in sound code (before being updated in PlayerFunc()). 
        Can cause local player sounds to play off-centre.
        TODO: Might need to be done elsewhere too?
    */
    auto pActor = PlayerList[nLocalPlayer].pActor;
    initpos = pActor->spr.pos;
    inita = pActor->spr.angle;
    initsectp = pActor->sector();
}

void SetQuake(DExhumedActor* pActor, int nVal)
{
    for (int i = 0; i < nTotalPlayers; i++)
    {
        auto nSqrt = ((PlayerList[i].pActor->spr.pos.XY() - pActor->spr.pos.XY()) * (1. / 16.)).Length();
        double eax = nVal;

        if (nSqrt)
        {
            eax = eax / nSqrt;

            if (eax >= 1)
            {
                if (eax > 15)
                {
                    eax = 15;
                }
            }
            else
            {
                eax = 0;
            }
        }

        if (eax > nQuake[i])
        {
            nQuake[i] = eax;
        }
    }
}

Collision AngleChase(DExhumedActor* pActor, DExhumedActor* pActor2, int ebx, int ecx, int push1) 
{
    int nClipType = pActor->spr.statnum != 107;

    /* bjd - need to handle cliptype to clipmask change that occured in later build engine version */
    if (nClipType == 1) {
        nClipType = CLIPMASK1;
    }
    else {
        nClipType = CLIPMASK0;
    }

    int nAngle;

    if (pActor2 == nullptr)
    {
        pActor->angle2 = 0;
        nAngle = pActor->int_ang();
    }
    else
    {
        int nHeight = tileHeight(pActor2->spr.picnum) * pActor2->spr.yrepeat * 2;

		auto vect = pActor2->spr.pos.XY() - pActor->spr.pos.XY();
        int nMyAngle = getangle(vect);

        int nSqrt = int(vect.Length() * worldtoint);

        int var_18 = getangle(nSqrt, ((pActor2->int_pos().Z - nHeight) - pActor->int_pos().Z) >> 8);

        int nAngDelta = AngleDelta(pActor->int_ang(), nMyAngle, 1024);
        int nAngDelta2 = abs(nAngDelta);

        if (nAngDelta2 > 63)
        {
            nAngDelta2 = abs(nAngDelta >> 6);

            ebx /= nAngDelta2;

            if (ebx < 5) {
                ebx = 5;
            }
        }

        int nAngDeltaC = abs(nAngDelta);

        if (nAngDeltaC > push1)
        {
            if (nAngDelta >= 0)
                nAngDelta = push1;
            else
                nAngDelta = -push1;
        }

        nAngle = (nAngDelta + pActor->int_ang()) & kAngleMask;
        int nAngDeltaD = AngleDelta(pActor->angle2, var_18, 24);

        pActor->angle2 = (pActor->angle2 + nAngDeltaD) & kAngleMask;
    }

    pActor->set_int_ang(nAngle);

    int eax = abs(bcos(pActor->angle2));

    int x = ((bcos(nAngle) * ebx) >> 14) * eax;
    int y = ((bsin(nAngle) * ebx) >> 14) * eax;

    int xshift = x >> 8;
    int yshift = y >> 8;

    uint32_t sqrtNum = xshift * xshift + yshift * yshift;

    if (sqrtNum > INT_MAX)
    {
        DPrintf(DMSG_WARNING, "%s %d: overflow\n", __func__, __LINE__);
        sqrtNum = INT_MAX;
    }

    int z = bsin(pActor->int_zvel()) * ksqrt(sqrtNum);

    return movesprite(pActor, x >> 2, y >> 2, (z >> 13) + bsin(ecx, -5), 0, 0, nClipType);
}

int GetWallNormal(walltype* pWall)
{
    int nAngle = getangle(pWall->delta());
    return (nAngle + 512) & kAngleMask;
}

DVector3 WheresMyMouth(int nPlayer, sectortype **sectnum)
{
    auto pActor = PlayerList[nPlayer].pActor;
    double height = GetActorHeight(pActor) * 0.5;

    *sectnum = pActor->sector();
	auto pos = pActor->spr.pos.plusZ(-height * zinttoworld);

    Collision scratch;
    clipmove(pos, sectnum,
        bcos(pActor->int_ang(), 7),
        bsin(pActor->int_ang(), 7),
        5120, 1280, 1280, CLIPMASK1, scratch);
	return pos;
}

void InitChunks()
{
    nCurChunkNum = 0;
    memset(nChunkSprite,   0, sizeof(nChunkSprite));
    memset(nBodyGunSprite, 0, sizeof(nBodyGunSprite));
    memset(nBodySprite,    0, sizeof(nBodySprite));
    nCurBodyNum    = 0;
    nCurBodyGunNum = 0;
    nBodyTotal  = 0;
    nChunkTotal = 0;
}

DExhumedActor* GrabBodyGunSprite()
{
    DExhumedActor* pActor = nBodyGunSprite[nCurBodyGunNum];
    if (pActor == nullptr)
    {
        pActor = insertActor(0, 899);
        nBodyGunSprite[nCurBodyGunNum] = pActor;

        pActor->spr.lotag = -1;
        pActor->spr.intowner = -1;
    }
    else
    {
        DestroyAnim(pActor);

        pActor->spr.lotag = -1;
        pActor->spr.intowner = -1;
    }

    nCurBodyGunNum++;
    if (nCurBodyGunNum >= 50) { // TODO - enum/define
        nCurBodyGunNum = 0;
    }

    pActor->spr.cstat = 0;

    return pActor;
}

DExhumedActor* GrabBody()
{
	DExhumedActor* pActor = nullptr;
    do
    {
        pActor = nBodySprite[nCurBodyNum];

        if (pActor == nullptr)
        {
            pActor = insertActor(0, 899);
            nBodySprite[nCurBodyNum] = pActor;
            pActor->spr.cstat = CSTAT_SPRITE_INVISIBLE;
        }


        nCurBodyNum++;
        if (nCurBodyNum >= 50) {
            nCurBodyNum = 0;
        }
    } while (pActor->spr.cstat & CSTAT_SPRITE_BLOCK_ALL);

    if (nBodyTotal < 50) {
        nBodyTotal++;
    }

    pActor->spr.cstat = 0;
    return pActor;
}

DExhumedActor* GrabChunkSprite()
{
    DExhumedActor* pActor = nChunkSprite[nCurChunkNum];

    if (pActor == nullptr)
    {
        pActor = insertActor(0, 899);
		nChunkSprite[nCurChunkNum] = pActor;
    }
    else if (pActor->spr.statnum)
    {
// TODO	MonoOut("too many chunks being used at once!\n");
        return nullptr;
    }

    ChangeActorStat(pActor, 899);

    nCurChunkNum++;
    if (nCurChunkNum >= kMaxMoveChunks)
        nCurChunkNum = 0;

    if (nChunkTotal < kMaxMoveChunks)
        nChunkTotal++;

    pActor->spr.cstat = CSTAT_SPRITE_YCENTER;

    return pActor;
}

DExhumedActor* BuildCreatureChunk(DExhumedActor* pSrc, int nPic, bool bSpecial)
{
    auto pActor = GrabChunkSprite();

    if (pActor == nullptr) {
        return nullptr;
    }
    pActor->spr.pos = pSrc->spr.pos;

    ChangeActorSect(pActor, pSrc->sector());

    pActor->spr.cstat = CSTAT_SPRITE_YCENTER;
    pActor->spr.shade = -12;
    pActor->spr.pal = 0;

    pActor->vel.X = ((RandomSize(5) - 16) << 3);
    pActor->vel.Y = ((RandomSize(5) - 16) << 3);
    pActor->vel.Z = -(RandomSize(8) / 32. + 16);

    if (bSpecial)
    {
        pActor->vel.X *= 4;
        pActor->vel.Y *= 4;
        pActor->vel.Z *= 2;
    }

    pActor->spr.xrepeat = 64;
    pActor->spr.yrepeat = 64;
    pActor->spr.xoffset = 0;
    pActor->spr.yoffset = 0;
    pActor->spr.picnum = nPic;
    pActor->spr.lotag = runlist_HeadRun() + 1;
    pActor->set_const_clipdist(40);

//	GrabTimeSlot(3);

    pActor->spr.extra = -1;
    pActor->spr.intowner = runlist_AddRunRec(pActor->spr.lotag - 1, pActor, 0xD0000);
    pActor->spr.hitag = runlist_AddRunRec(NewRun, pActor, 0xD0000);

    return pActor;
}

void AICreatureChunk::Tick(RunListEvent* ev)
{
    auto pActor = ev->pObjActor;
    if (!pActor) return;

    Gravity(pActor);

    auto pSector = pActor->sector();
    pActor->spr.pal = pSector->ceilingpal;

    auto nVal = movesprite(pActor, pActor->vel, 1024., 2560, -2560, CLIPMASK1);

    if (pActor->spr.pos.Z >= pSector->floorz)
    {
        // re-grab this variable as it may have changed in movesprite(). Note the check above is against the value *before* movesprite so don't change it.
        pSector = pActor->sector();

        pActor->vel.X = 0;
        pActor->vel.Y = 0;
        pActor->vel.Z = 0;
        pActor->spr.pos.Z = pSector->floorz;
    }
    else
    {
        if (!nVal.type && !nVal.exbits)
            return;

        DAngle nAngle;

        if (nVal.exbits & kHitAux2)
        {
            pActor->spr.cstat = CSTAT_SPRITE_INVISIBLE;
        }
        else
        {
            if (nVal.exbits & kHitAux1)
            {
                pActor->vel.X *= 0.5;
                pActor->vel.Y *= 0.5;
                pActor->vel.Z = -pActor->vel.Z;
                return;
            }
            else if (nVal.type == kHitSprite)
            {
                nAngle = nVal.actor()->spr.angle;
            }
            else if (nVal.type == kHitWall)
            {
                nAngle = DAngle::fromBuild(GetWallNormal(nVal.hitWall));
            }
            else
            {
                return;
            }

            // loc_16E0C
			double nSqrt = pActor->vel.Length();


			pActor->vel.XY() = nAngle.ToVector() * nSqrt * 0.5;
            return;
        }
    }

    runlist_DoSubRunRec(pActor->spr.intowner);
    runlist_FreeRun(pActor->spr.lotag - 1);
    runlist_SubRunRec(pActor->spr.hitag);

    ChangeActorStat(pActor, 0);
    pActor->spr.hitag = 0;
    pActor->spr.lotag = 0;
}

DExhumedActor* UpdateEnemy(DExhumedActor** ppEnemy)
{
    if (*ppEnemy)
    {
        if (!((*ppEnemy)->spr.cstat & CSTAT_SPRITE_BLOCK_ALL)) {
            *ppEnemy = nullptr;
        }
    }

    return *ppEnemy;
}
END_PS_NS
