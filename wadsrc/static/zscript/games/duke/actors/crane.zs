
class DukeCrane : DukeActor
{
	default
	{
		spriteset "CRANE", "CRANE1", "CRANE2";
		statnum STAT_STANDABLE;
	}
	
	enum EPic
	{
		PIC_DEFAULT = 0,
		PIC_OPEN = 1,
		PIC_CLOSED = 2,
	}

	Vector3 cranepos;
	Vector2 polepos;
	DukeCranePole poleactor;

	//---------------------------------------------------------------------------
	//
	// 
	//
	//---------------------------------------------------------------------------

	override void Initialize()
	{
		let sect = sector;
		cstat |= CSTAT_SPRITE_BLOCK_ALL | CSTAT_SPRITE_ONE_SIDE;

		setspritepic(PIC_CLOSED);
		SetZ(sect.ceilingz + 48);

		cranepos = act.pos;
		poleactor = null;

		let it = DukeLevel.CreateStatIterator(STAT_DEFAULT);
		for(DukeActor actk; actk = it.Next();)
		{
			if (hitag == hitag && actk is "DukeCranePole")
			{
				poleactor = actk;

				temp_sect = actk.sector;

				actk.xrepeat = 48;
				actk.yrepeat = 128;
				actk.shade = shade;

				polepos = actk.xy();
				actk.copypos(self);
				break;
			}
		}

		ownerActor = null;
		extra = 8;
	}

	//---------------------------------------------------------------------------
	//
	//
	//
	//---------------------------------------------------------------------------

	override void Tick()
	{
		int x;
		let pos = self.pos();

		//temp_data[0] = state
		//temp_data[1] = checking sector number

		if (xvel) getglobalz();

		if (temp_data[0] == 0) //Waiting to check the sector
		{
			let it = DukeLevel.CreateSectIterator(temp_sect);
			for(DukeActor a2; a2 = it.Next();)
			{
				switch (a2.statnum)
				{
				case STAT_ACTOR:
				case STAT_ZOMBIEACTOR:
				case STAT_STANDABLE:
				case STAT_PLAYER:
					ang = getangle(polepos.X - pos.X, polepos.Y - pos.Y);
					a2.SetPosition(( polepos.X, polepos.Y, a2.Z() ));
					temp_data[0]++;
					return;
				}
			}
		}

		else if (temp_data[0] == 1)
		{
			if (xvel < 184)
			{
				setSpritePic(PIC_OPEN);
				xvel += 8;
			}
			//IFMOVING;	// JBF 20040825: see my rant above about this
			ssp(CLIPMASK0);
			if (sector == temp_sect)
				temp_data[0]++;
		}
		else if (temp_data[0] == 2 || temp_data[0] == 7)
		{
			addZ(4 + 2);

			if (temp_data[0] == 2)
			{
				if ((sector.floorz() - pos.Z) < 64)
					if (spritesetpic > 0) setspritepic(spritesetpic - 1);

				if ((sector.floorz() - pos.Z) < (16 + 4))
					temp_data[0]++;
			}
			if (temp_data[0] == 7)
			{
				if ((sector.floorz() - pos.Z) < 64)
				{
					if (spritesetpic > 0) setspritepic(spritesetpic - 1);
					else
					{
						if (IsActiveCrane())
						{
							int p = findplayer(actor);
							ps[p].actor.PlayActorSound(isRR() ? 390 : DUKE_GRUNT);
							if (ps[p].on_crane == actor)
								ps[p].on_crane = null;
						}
						temp_data[0]++;
						SetActiveCrane(false);
					}
				}
			}
		}
		else if (temp_data[0] == 3)
		{
			setspritepic(spritesetpic + 1);
			if (spritesetpic == 2)
			{
				int p = checkcursectnums(temp_sect);
				if (p >= 0 && ps[p].on_ground)
				{
					SetActiveCrane(true);
					ps[p].on_crane = actor;
					ps[p].actor.PlayActorSound(isRR() ? 390 : DUKE_GRUNT);
					ps[p].angle.settarget(ang + 1024);
				}
				else
				{
					let it = DukeLevel.CreateSectIterator(temp_sect);
					for(DukeActor a2; a2 = it.Next();)
					{
						switch (a2.statnum)
						{
						case 1:
						case 6:
							SetOwner(a2);
							break;
						}
					}
				}

				temp_data[0]++;//Grabbed the sprite
				temp_data[2] = 0;
				return;
			}
		}
		else if (temp_data[0] == 4) //Delay before going up
		{
			temp_data[2]++;
			if (temp_data[2] > 10)
				temp_data[0]++;
		}
		else if (temp_data[0] == 5 || temp_data[0] == 8)
		{
			if (temp_data[0] == 8 && spritesetpic < 2)
				if ((sector.floorz - pos.Z) > 32)
					setspritepic(spritesetpic + 1);

			if (pos.Z < cranepos.Z)
			{
				temp_data[0]++;
				xvel = 0;
			}
			else
				addZ(4 + 2);
		}
		else if (temp_data[0] == 6)
		{
			if (xvel < 192)
				xvel += 8;
			ang = getangle(cranepos.X - pos.X, cranepos.Y - pos.Y);
			ssp(actor, CLIPMASK0);
			if (((pos.X - cranepos.X) * (pos.X - cranepos.X) + (pos.Y - cranepos.Y) * (pos.Y - cranepos.Y)) < (0.5 * 0.5))
				temp_data[0]++;
		}

		else if (temp_data[0] == 9)
			temp_data[0] = 0;

		if (poleactor)
			poleactor.SetActor(poleactor, (pos.X, pos.Y, pos.Z - 34));

		if (Owner != null || IsActiveCrane())
		{
			int p = findplayer(actor);

			int j = ifhitbyweapon();
			if (j >= 0)
			{
				if (IsActiveCrane())
					if (ps[p].on_crane == actor)
						ps[p].on_crane = null;
				SetActiveCrane(false);
				setspritepic(0);
				return;
			}

			if (Owner != null)
			{
				SetActor(Owner, pos);
				Owner.backuppos();
				zvel = 0;
			}
			else if (IsActiveCrane())
			{
				let ang = ps[p].angleAsBuild();	// temporary hack
				ps[p].backuppos();
				let newpos = (pos.X - bcos(ang, -6), pos.Y - bsin(ang, -6), pos.Z + 2);
				ps[p].setposition(newpos);
				ps[p].actor.SetPosition(newpos);
				ps[p].cursector = ps[p].actor.sectp;
			}
		}
	}
}

class DukeCranePole : DukeActor
{
	default
	{
		spriteset "CRANEPOLE";
	}
}
