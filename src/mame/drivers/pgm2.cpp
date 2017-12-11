// license:BSD-3-Clause
// copyright-holders:David Haywood, Xing Xing, Andreas Naive
/* PGM 2 hardware.

    Motherboard is bare bones stuff, and does not contain any ROMs.
    The IGS036 used by the games is an ARM based CPU, like IGS027A used on PGM1 it has internal ROM.
    Decryption should be correct in most cases.
    The ARM appears to be ARMv5T, probably an ARM9.

    PGM2 Motherboard Components:

     IS61LV25616AL(SRAM)
     IGS037(GFX PROCESSOR)
     YMZ774-S(SOUND)
     R5F21256SN(extra MCU for protection and ICcard communication)
      - Appears to be referred to by the games as MPU

    Cartridges
     IGS036 (MAIN CPU) (differs per game, internal code)
     ROMs
     Custom program ROM module (KOV3 only)
      - on some games ROM socket contains Flash ROM + SRAM

     QFP100 chip (Xlinx CPLD)

     Single PCB versions of some of the titles were also available

    Only 5 Games were released for this platform, 3 of which are just updates / re-releases of older titles!
    The platform has since been superseded by PGM3, see pgm3.cpp

    Oriental Legend 2
    The King of Fighters '98 - Ultimate Match - Hero
    Knights of Valour 2 New Legend
    Dodonpachi Daioujou Tamashii
    Knights of Valour 3

    These were only released as single board PGM2 based hardware, seen for sale in Japan for around $250-$300

    Jigsaw World Arena
    Puzzle of Ocha / Ochainu No Pazuru


    ToDo (emulation issues):

    Support remaining games (need IGS036 dumps)
    Identify which regions each game was released in and either dump alt. internal ROMs for each region, or
      create them until that can be done.
    properly implement RTC (integrated into the CPU)
    Memory Card system (there's an MCU on the motherboard that will need simulating or dumping somehow)
    Verify Sprite Zoom (check exactly which pixels are doubled / missed on hardware for flipped , non-flipped cases etc.)
    Simplify IGS036 encryption based on tables in internal roms
    Fix ARM? bug that means Oriental Legend 2 needs a patch (might also be that it needs the card reader, and is running a
     codepath that would not exist in a real environment at the moment)
    Fix Save States (is this a driver problem or an ARM core problem, they don't work unless you get through the startup tests)

	Debug features (require DIP SW1:8 On and SW1:1 Off):
	- QC TEST mode: hold P1 A+B during boot
	- Debug/Cheat mode: hold P1 B+C during boot, when ingame pressing P1 Start skips to next location, where might be more unknown debug features.
	works for both currently dumped games (orleg2, kov2nl)


	Holographic Stickers

	The IGS036 CPUs have holographic stickers on them, there is a number printed on each sticker but it doesn't seem connected to the
	game code / revision contained within, it might just be to mark the date the board was produced as it seems to coincide with the
	design of the hologram.  For reference the ones being used for dumping are

	Dodonpachi Daioujou Tamashi (China) - W10
	King of Fighter 98 UMH (China) - C11
	Knights of Valour 2 (China) - V21
	Knights of Valour 3 (China) - V21
	Oriental Legend 2 (Oversea) - V21
	Oriental Legend 2 (China) - A8

*/

#include "includes/pgm2.h"

// checked on startup, or doesn't boot
READ32_MEMBER(pgm2_state::unk_startup_r)
{
	logerror("%s: unk_startup_r\n", machine().describe_context().c_str());
	return 0x00000180;
}

READ32_MEMBER(pgm2_state::rtc_r)
{
	// write to FFFFFD20 if bit 18 set (0x40000) probably reset this RTC timer
	// TODO: somehow hook here current time/date, which is a bit complicated because value is relative, later to it added "base time" stored in SRAM
	return machine().time().seconds();
}

READ8_MEMBER(pgm2_state::encryption_r)
{
	return m_encryption_table[offset];
}

WRITE8_MEMBER(pgm2_state::encryption_w)
{
	m_encryption_table[offset] = data;
}

WRITE32_MEMBER(pgm2_state::sprite_encryption_w)
{
	COMBINE_DATA(&m_spritekey);

	if (!m_sprite_predecrypted)
		m_realspritekey = BITSWAP32(m_spritekey ^ 0x90055555, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31);
}

WRITE32_MEMBER(pgm2_state::encryption_do_w)
{
	if (m_has_decrypted == 0)
	{
		igs036_decryptor decrypter(m_encryption_table);
		decrypter.decrypter_rom(memregion("user1"));
		m_has_decrypted = 1;
	}
}


INTERRUPT_GEN_MEMBER(pgm2_state::igs_interrupt)
{
	m_arm_aic->set_irq(0x47);
}

WRITE16_MEMBER(pgm2_state::share_bank_w)
{
	COMBINE_DATA(&m_share_bank);
}

READ8_MEMBER(pgm2_state::shareram_r)
{
	return m_shareram[offset + (m_share_bank & 1) * 128];
}
WRITE8_MEMBER(pgm2_state::shareram_w)
{
	m_shareram[offset + (m_share_bank & 1) * 128] = data;
}


TIMER_DEVICE_CALLBACK_MEMBER(pgm2_state::igs_interrupt2)
{
	m_arm_aic->set_irq(0x46);
}

// "MPU" MCU HLE starts here
// command delays are far from correct, might not work in other games
// command results probably incorrect (except for explicit checked bytes)
void pgm2_state::mcu_command(address_space &space, bool is_command)
{
	uint8_t cmd = m_mcu_regs[0] & 0xff;
	//	if (is_command && cmd != 0xf6)
	//		logerror("MCU command %08x %08x\n", m_mcu_regs[0], m_mcu_regs[1]);

	if (is_command)
	{
		m_mcu_last_cmd = cmd;
		uint8_t status = 0xf7; // "command accepted" status
		int delay = 1;

		uint8_t arg1 = m_mcu_regs[0] >> 8;
		uint8_t arg2 = m_mcu_regs[0] >> 16;
		uint8_t arg3 = m_mcu_regs[0] >> 24;
		switch (cmd)
		{
		case 0xf6:	// get result
			m_mcu_regs[3] = m_mcu_result0;
			m_mcu_regs[4] = m_mcu_result1;
			m_mcu_last_cmd = 0;
			break;
		case 0xe0: // command port test
			m_mcu_result0 = m_mcu_regs[0];
			m_mcu_result1 = m_mcu_regs[1];
			delay = 30;  // such quite long delay is needed for debug codes check routine
			break;
		case 0xe1: // shared ram access (unimplemented)
		{
			// MCU access to RAM shared at 0x30100000, 2x banks, in the same time CPU and MCU access different banks
			uint8_t mode = m_mcu_regs[0] >> 16; // 0 - ???, 1 - read, 2 - write
			uint8_t data = m_mcu_regs[0] >> 24;
			if (mode == 2)
			{
				// where is offset ? so far assume this command fill whole page
				memset(&m_shareram[(~m_share_bank & 1) * 128], data, 128);
			}
			m_mcu_result0 = cmd;
			m_mcu_result1 = 0;
		}
		break;
		// unknown / unimplemented, all C0-C9 commands is IC Card RW related
		// (m_mcu_regs[0] >> 8) & 0xff - target RW unit (player)
		case 0xc0: // insert card or/and check card presence. result: F7 - ok, F4 - no card
			if (m_memcard_device[arg1 & 3]->present() == -1)
				status = 0xf4;
			m_mcu_result0 = cmd;
			break;
		case 0xc1: // check ready/busy ?
			m_mcu_result0 = cmd;
			break;
		case 0xc2: // read data to shared ram, args - offset, len
			for (int i = 0; i < arg3; i++)
			{
				if (m_memcard_device[arg1 & 3]->present() != -1)
					m_shareram[i + (~m_share_bank & 1) * 128] = m_memcard_device[arg1 & 3]->read(space, arg2 + i);
			}

			m_mcu_result0 = cmd;
			break;
		case 0xc3: // save data from shared ram, args - offset, len
			for (int i = 0; i < arg3; i++)
			{
				if (m_memcard_device[arg1 & 3]->present() != -1)
					m_memcard_device[arg1 & 3]->write(space, arg2 + i, m_shareram[i + (~m_share_bank & 1) * 128]);
			}
			m_mcu_result0 = cmd;
			break;
		case 0xc7: // get card ID?, no args, result1 expected to be fixed value for new card
			m_mcu_result1 = 0xf81f0000;
			m_mcu_result0 = cmd;
			break;
		case 0xc8: // write byte, args - offset, data byte
			if (m_memcard_device[arg1 & 3]->present() != -1)
				m_memcard_device[arg1 & 3]->write(space, arg2, arg3);

			m_mcu_result0 = cmd;
			break;
		case 0xc4: // not used
		case 0xc5: // set new password?, args - offset, data byte (offs 0 - always 7, 1-3 password)
		case 0xc6: // not used
		case 0xc9: // card authentication, args - 3 byte password, ('I','G','S' for new cards)
			m_mcu_result0 = cmd;
			break;
		default:
			logerror("MCU unknown command %08x %08x\n", m_mcu_regs[0], m_mcu_regs[1]);
			status = 0xf4; // error
			break;
		}
		m_mcu_regs[3] = (m_mcu_regs[3] & 0xff00ffff) | (status << 16);
		m_mcu_timer->adjust(attotime::from_msec(delay));
	}
	else // next step
	{
		if (m_mcu_last_cmd)
		{
			m_mcu_regs[3] = (m_mcu_regs[3] & 0xff00ffff) | 0x00F20000; 	// set "command done and return data" status
			m_mcu_timer->adjust(attotime::from_usec(100));
			m_mcu_last_cmd = 0;
		}
	}
}

READ32_MEMBER(pgm2_state::mcu_r)
{
	return m_mcu_regs[(offset >> 15) & 7];
}

WRITE32_MEMBER(pgm2_state::mcu_w)
{
	int reg = (offset >> 15) & 7;
	COMBINE_DATA(&m_mcu_regs[reg]);

	if (reg == 2 && m_mcu_regs[2]) // irq to mcu
		mcu_command(space, true);
	if (reg == 5 && m_mcu_regs[5]) // ack to mcu (written at the end of irq handler routine)
		mcu_command(space, false);
}

static ADDRESS_MAP_START( pgm2_map, AS_PROGRAM, 32, pgm2_state )
	AM_RANGE(0x00000000, 0x00003fff) AM_ROM //AM_REGION("user1", 0x00000) // internal ROM

	AM_RANGE(0x02000000, 0x0200ffff) AM_RAM AM_SHARE("sram") // 'battery ram' (in CPU?)

	AM_RANGE(0x03600000, 0x036bffff) AM_READWRITE(mcu_r, mcu_w)

	AM_RANGE(0x03900000, 0x03900003) AM_READ_PORT("INPUTS0")
	AM_RANGE(0x03a00000, 0x03a00003) AM_READ_PORT("INPUTS1")

	AM_RANGE(0x10000000, 0x10ffffff) AM_ROM AM_REGION("user1", 0) // external ROM
	AM_RANGE(0x20000000, 0x2007ffff) AM_RAM AM_SHARE("mainram")

	AM_RANGE(0x30000000, 0x30001fff) AM_RAM AM_SHARE("sp_videoram") // spriteram ('move' ram in test mode)

	AM_RANGE(0x30020000, 0x30021fff) AM_RAM_WRITE(bg_videoram_w) AM_SHARE("bg_videoram")
	AM_RANGE(0x30040000, 0x30045fff) AM_RAM_WRITE(fg_videoram_w) AM_SHARE("fg_videoram")

	AM_RANGE(0x30060000, 0x30063fff) AM_RAM_DEVWRITE("sp_palette", palette_device, write) AM_SHARE("sp_palette")

	AM_RANGE(0x30080000, 0x30081fff) AM_RAM_DEVWRITE("bg_palette", palette_device, write) AM_SHARE("bg_palette")

	AM_RANGE(0x300a0000, 0x300a07ff) AM_RAM_DEVWRITE("tx_palette", palette_device, write) AM_SHARE("tx_palette")

	AM_RANGE(0x300c0000, 0x300c01ff) AM_RAM AM_SHARE("sp_zoom") // sprite zoom table - it uploads the same data 4x, maybe xshrink,xgrow,yshrink,ygrow or just redundant mirrors

	/* linescroll ram - it clears to 0x3bf on startup which is enough bytes for 240 lines if each rowscroll value was 8 bytes, but each row is 4,
	so only half of this is used? or tx can do it too (unlikely, as orl2 writes 256 lines of data) maybe just bad mem check bounds on orleg2.
	It reports pass even if it fails the first byte but if the first byte passes it attempts to test 0x10000 bytes, which is far too big so
	what is the real size? */
	AM_RANGE(0x300e0000, 0x300e03ff) AM_RAM AM_SHARE("lineram") AM_MIRROR(0x000fc00)

	AM_RANGE(0x30100000, 0x301000ff) AM_READWRITE8(shareram_r, shareram_w, 0x00ff00ff)

	AM_RANGE(0x30120000, 0x30120003) AM_RAM AM_SHARE("bgscroll") // scroll
	AM_RANGE(0x30120030, 0x30120033) AM_WRITE16(share_bank_w, 0xffff0000)
	AM_RANGE(0x30120038, 0x3012003b) AM_WRITE(sprite_encryption_w)
	// there are other 0x301200xx regs

	AM_RANGE(0x40000000, 0x40000003) AM_DEVREADWRITE8("ymz774", ymz774_device, read, write, 0xffffffff)

	// internal to IGS036? - various other writes down here on startup too - could be other standard ATMEL peripherals like the ARM_AIC mixed with custom bits
	AM_RANGE(0xffffec00, 0xffffec5f) AM_RAM
	AM_RANGE(0xfffffc00, 0xfffffcff) AM_READWRITE8(encryption_r,encryption_w, 0xffffffff) // confirmed as encryption table for main program rom (see code at 3950)

	AM_RANGE(0xfffff000, 0xfffff14b) AM_DEVICE("arm_aic", arm_aic_device, regs_map)

	AM_RANGE(0xfffff430, 0xfffff433) AM_WRITENOP // often
	AM_RANGE(0xfffff434, 0xfffff437) AM_WRITENOP // often

	AM_RANGE(0xfffffd28, 0xfffffd2b) AM_READ(rtc_r)

	AM_RANGE(0xfffffa08, 0xfffffa0b) AM_WRITE(encryption_do_w) // after uploading encryption? table might actually send it or enable external ROM? when read bits0-1 is ROM board status (0 if OK)
	AM_RANGE(0xfffffa0c, 0xfffffa0f) AM_READ(unk_startup_r)
ADDRESS_MAP_END

static INPUT_PORTS_START( pgm2 )
	PORT_START("INPUTS0")
	PORT_BIT( 0x00000001, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_PLAYER(1)
	PORT_BIT( 0x00000002, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_PLAYER(1)
	PORT_BIT( 0x00000004, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_PLAYER(1)
	PORT_BIT( 0x00000008, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(1)
	PORT_BIT( 0x00000010, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(1)
	PORT_BIT( 0x00000020, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(1)
	PORT_BIT( 0x00000040, IP_ACTIVE_LOW, IPT_BUTTON3 ) PORT_PLAYER(1)
	PORT_BIT( 0x00000080, IP_ACTIVE_LOW, IPT_BUTTON4 ) PORT_PLAYER(1)
	PORT_BIT( 0x00000100, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x00000200, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x00000400, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_PLAYER(2)
	PORT_BIT( 0x00000800, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_PLAYER(2)
	PORT_BIT( 0x00001000, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_PLAYER(2)
	PORT_BIT( 0x00002000, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(2)
	PORT_BIT( 0x00004000, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(2)
	PORT_BIT( 0x00008000, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(2)
	PORT_BIT( 0x00010000, IP_ACTIVE_LOW, IPT_BUTTON3 ) PORT_PLAYER(2)
	PORT_BIT( 0x00020000, IP_ACTIVE_LOW, IPT_BUTTON4 ) PORT_PLAYER(2)
	PORT_BIT( 0x00040000, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x00080000, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x00100000, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_PLAYER(3)
	PORT_BIT( 0x00200000, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_PLAYER(3)
	PORT_BIT( 0x00400000, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_PLAYER(3)
	PORT_BIT( 0x00800000, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(3)
	PORT_BIT( 0x01000000, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(3)
	PORT_BIT( 0x02000000, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(3)
	PORT_BIT( 0x04000000, IP_ACTIVE_LOW, IPT_BUTTON3 ) PORT_PLAYER(3)
	PORT_BIT( 0x08000000, IP_ACTIVE_LOW, IPT_BUTTON4 ) PORT_PLAYER(3)
	PORT_BIT( 0x10000000, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x20000000, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x40000000, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x80000000, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_START("INPUTS1")
	PORT_BIT( 0x00000001, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_PLAYER(4)
	PORT_BIT( 0x00000002, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_PLAYER(4)
	PORT_BIT( 0x00000004, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_PLAYER(4)
	PORT_BIT( 0x00000008, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(4)
	PORT_BIT( 0x00000010, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(4)
	PORT_BIT( 0x00000020, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(4)
	PORT_BIT( 0x00000040, IP_ACTIVE_LOW, IPT_BUTTON3 ) PORT_PLAYER(4)
	PORT_BIT( 0x00000080, IP_ACTIVE_LOW, IPT_BUTTON4 ) PORT_PLAYER(4)
	PORT_BIT( 0x00000100, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x00000200, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x00000400, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0x00000800, IP_ACTIVE_LOW, IPT_START2 )
	PORT_BIT( 0x00001000, IP_ACTIVE_LOW, IPT_START3 )
	PORT_BIT( 0x00002000, IP_ACTIVE_LOW, IPT_START4 )
	PORT_BIT( 0x00004000, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x00008000, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x00010000, IP_ACTIVE_LOW, IPT_COIN3 )
	PORT_BIT( 0x00020000, IP_ACTIVE_LOW, IPT_COIN4 )
	PORT_BIT( 0x00040000, IP_ACTIVE_LOW, IPT_SERVICE1 ) // test key p1+p2
	PORT_BIT( 0x00080000, IP_ACTIVE_LOW, IPT_SERVICE2 ) // test key p3+p4
	PORT_BIT( 0x00100000, IP_ACTIVE_LOW, IPT_SERVICE3 ) // service key p1+p2
	PORT_BIT( 0x00200000, IP_ACTIVE_LOW, IPT_SERVICE4 ) // service key p3+p4
	PORT_BIT( 0x00400000, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x00800000, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_SERVICE( 0x01000000, IP_ACTIVE_LOW ) PORT_DIPLOCATION("SW1:1")
	PORT_DIPNAME( 0x02000000, 0x02000000, "Music" )  PORT_DIPLOCATION("SW1:2")
	PORT_DIPSETTING(          0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x02000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x04000000, 0x04000000, "Voice" )  PORT_DIPLOCATION("SW1:3")
	PORT_DIPSETTING(          0x00000000, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x04000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x08000000, 0x08000000, "Free" )  PORT_DIPLOCATION("SW1:4")
	PORT_DIPSETTING(          0x08000000, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x10000000, 0x10000000, "Stop" )  PORT_DIPLOCATION("SW1:5")
	PORT_DIPSETTING(          0x10000000, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x20000000, 0x20000000, DEF_STR( Unused ) )  PORT_DIPLOCATION("SW1:6")
	PORT_DIPSETTING(          0x20000000, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x40000000, 0x40000000, DEF_STR( Unused ) )  PORT_DIPLOCATION("SW1:7")
	PORT_DIPSETTING(          0x40000000, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x80000000, 0x80000000, "Debug" )  PORT_DIPLOCATION("SW1:8")
	PORT_DIPSETTING(          0x80000000, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00000000, DEF_STR( On ) )
INPUT_PORTS_END


WRITE_LINE_MEMBER(pgm2_state::irq)
{
//  printf("irq\n");
	if (state == ASSERT_LINE) m_maincpu->set_input_line(ARM7_IRQ_LINE, ASSERT_LINE);
	else m_maincpu->set_input_line(ARM7_IRQ_LINE, CLEAR_LINE);
}

void pgm2_state::machine_start()
{
	save_item(NAME(m_encryption_table));
	save_item(NAME(m_has_decrypted));
	save_item(NAME(m_spritekey));
	save_item(NAME(m_realspritekey));
	save_item(NAME(m_mcu_regs));
	save_item(NAME(m_mcu_result0));
	save_item(NAME(m_mcu_result1));
	save_item(NAME(m_mcu_last_cmd));
	save_item(NAME(m_shareram));
	save_item(NAME(m_share_bank));

	m_memcard_device[0] = m_memcard0;
	m_memcard_device[1] = m_memcard1;
	m_memcard_device[2] = m_memcard2;
	m_memcard_device[3] = m_memcard3;
}

void pgm2_state::machine_reset()
{
	m_spritekey = 0;
	m_realspritekey = 0;
	m_mcu_last_cmd = 0;
	m_share_bank = 0;
}

static const gfx_layout tiles8x8_layout =
{
	8,8,
	RGN_FRAC(1,1),
	4,
	{ 0, 1, 2, 3 },
	{ 4, 0, 12, 8, 20, 16, 28, 24 },
	{ 0*32, 1*32, 2*32, 3*32, 4*32, 5*32, 6*32, 7*32 },
	32*8
};

static const gfx_layout tiles32x32x8_layout =
{
	32,32,
	RGN_FRAC(1,1),
	7,
	{ 1, 2, 3, 4, 5, 6, 7 },
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8, 8*8, 9*8, 10*8, 11*8, 12*8, 13*8, 14*8, 15*8,
		16*8, 17*8, 18*8, 19*8, 20*8, 21*8, 22*8, 23*8, 24*8, 25*8, 26*8, 27*8, 28*8, 29*8, 30*8, 31*8 },
	{ 0*256, 1*256, 2*256, 3*256, 4*256, 5*256, 6*256, 7*256, 8*256, 9*256, 10*256, 11*256, 12*256, 13*256, 14*256, 15*256,
		16*256, 17*256, 18*256, 19*256, 20*256, 21*256, 22*256, 23*256, 24*256, 25*256, 26*256, 27*256, 28*256, 29*256, 30*256, 31*256
	},
	256*32
};

static GFXDECODE_START( pgm2_tx )
	GFXDECODE_ENTRY( "tiles", 0, tiles8x8_layout, 0, 0x800/4/0x10 )
GFXDECODE_END

static GFXDECODE_START( pgm2_bg )
	GFXDECODE_ENTRY( "bgtile", 0, tiles32x32x8_layout, 0, 0x2000/4/0x80 )
GFXDECODE_END

static MACHINE_CONFIG_START( pgm2 )

	/* basic machine hardware */
	MCFG_CPU_ADD("maincpu", IGS036, 100000000) // ?? ARM based CPU, has internal ROM.
	MCFG_CPU_PROGRAM_MAP(pgm2_map)

	MCFG_CPU_VBLANK_INT_DRIVER("screen", pgm2_state,  igs_interrupt)
	MCFG_TIMER_DRIVER_ADD("mcu_timer", pgm2_state, igs_interrupt2)

	MCFG_ARM_AIC_ADD("arm_aic")
	MCFG_IRQ_LINE_CB(WRITELINE(pgm2_state, irq))

	/* video hardware */
	MCFG_SCREEN_ADD("screen", RASTER)
	MCFG_SCREEN_REFRESH_RATE(60)
	MCFG_SCREEN_VBLANK_TIME(ATTOSECONDS_IN_USEC(0))
	MCFG_SCREEN_SIZE(64*8, 32*8)
	MCFG_SCREEN_VISIBLE_AREA(0, 448-1, 0, 224-1)
	MCFG_SCREEN_UPDATE_DRIVER(pgm2_state, screen_update_pgm2)
	MCFG_SCREEN_VBLANK_CALLBACK(WRITELINE(pgm2_state, screen_vblank_pgm2))

	MCFG_GFXDECODE_ADD("gfxdecode2", "tx_palette", pgm2_tx)

	MCFG_GFXDECODE_ADD("gfxdecode3", "bg_palette", pgm2_bg)

	MCFG_PALETTE_ADD("sp_palette", 0x4000/4) // sprites
	MCFG_PALETTE_FORMAT(XRGB)

	MCFG_PALETTE_ADD("tx_palette", 0x800/4) // text
	MCFG_PALETTE_FORMAT(XRGB)

	MCFG_PALETTE_ADD("bg_palette", 0x2000/4) // bg
	MCFG_PALETTE_FORMAT(XRGB)

	MCFG_NVRAM_ADD_0FILL("sram")

	MCFG_SPEAKER_STANDARD_STEREO("lspeaker", "rspeaker")
	MCFG_YMZ774_ADD("ymz774", 16384000) // is clock correct ?
	MCFG_SOUND_ROUTE(0, "lspeaker", 1.0)
	MCFG_SOUND_ROUTE(1, "rspeaker", 1.0)

	MCFG_PGM2_MEMCARD_ADD("memcard_p1")
	MCFG_PGM2_MEMCARD_ADD("memcard_p2")
	MCFG_PGM2_MEMCARD_ADD("memcard_p3")
	MCFG_PGM2_MEMCARD_ADD("memcard_p4")


MACHINE_CONFIG_END

/* using macros for the video / sound roms because the locations never change between sets, and
   we're going to have a LOT of clones to cover all the internal rom regions and external rom revision
   combinations, so it keeps things readable */

// Oriental Legend 2

#define ORLEG2_VIDEO_SOUND_ROMS \
	ROM_REGION( 0x200000, "tiles", ROMREGION_ERASEFF ) \
	ROM_LOAD( "ig-a_text.u4",            0x00000000, 0x0200000, CRC(fa444c32) SHA1(31e5e3efa92d52bf9ab97a0ece51e3b77f52ce8a) ) \
	\
	ROM_REGION( 0x1000000, "bgtile", 0 ) \
	ROM_LOAD32_WORD( "ig-a_bgl.u35",     0x00000000, 0x0800000, CRC(083a8315) SHA1(0dba25e132fbb12faa59ced648c27b881dc73478) ) \
	ROM_LOAD32_WORD( "ig-a_bgh.u36",     0x00000002, 0x0800000, CRC(e197221d) SHA1(5574b1e3da4b202db725be906dd868edc2fd4634) ) \
	\
	ROM_REGION( 0x2000000, "sprites_mask", 0 ) /* 1bpp sprite mask data (packed) */ \
	ROM_LOAD32_WORD( "ig-a_bml.u12",     0x00000000, 0x1000000, CRC(113a331c) SHA1(ee6b31bb2b052cc8799573de0d2f0a83f0ab4f6a) ) \
	ROM_LOAD32_WORD( "ig-a_bmh.u16",     0x00000002, 0x1000000, CRC(fbf411c8) SHA1(5089b5cc9bbf6496ef1367c6255e63e9ab895117) ) \
	\
	ROM_REGION( 0x4000000, "sprites_colour", 0 ) /* sprite colour data (6bpp data, 2 bits unused except for 4 bytes that are randomly 0xff - check dump?) */ \
	ROM_LOAD32_WORD( "ig-a_cgl.u18",     0x00000000, 0x2000000, BAD_DUMP CRC(43501fa6) SHA1(58ccce6d393964b771fec3f5c583e3ede57482a3) ) \
	ROM_LOAD32_WORD( "ig-a_cgh.u26",     0x00000002, 0x2000000, BAD_DUMP CRC(7051d020) SHA1(3d9b24c6fda4c9699bb9f00742e0888059b623e1) ) \
	\
	ROM_REGION( 0x1000000, "ymz774", ROMREGION_ERASEFF ) /* ymz770 */ \
	ROM_LOAD16_WORD_SWAP( "ig-a_sp.u2",  0x00000000, 0x1000000, CRC(8250688c) SHA1(d2488477afc528aeee96826065deba2bce4f0a7d) ) \
	\
	ROM_REGION( 0x10000, "sram", 0 ) \
	ROM_LOAD( "xyj2_nvram",            0x00000000, 0x10000, CRC(ccccc71c) SHA1(585b5ccbf89dd28d8532da785d7c8af12f31c6d6) )

/* 
   External program revisions are CONFIRMED to be the same between regions, even if the label changes (localized game title + country specific extension code)
    
   Confirmed country codes used on labels
   FA = Oversea
   CN = China
   JP = Japan
   TW = Taiwan
  
*/

#define ORLEG2_PROGRAM_104(prefix, extension) \
	ROM_REGION( 0x1000000, "user1", 0 ) \
	ROM_LOAD( #prefix "_v104" #extension ".u7",  0x000000, 0x800000, CRC(7c24a4f5) SHA1(3cd9f9264ef2aad0869afdf096e88eb8d74b2570) )

#define ORLEG2_PROGRAM_103(prefix, extension) \
	ROM_REGION( 0x1000000, "user1", 0 ) \
	ROM_LOAD( #prefix "_v103" #extension ".u7",  0x000000, 0x800000, CRC(21c1fae8) SHA1(36eeb7a5e8dc8ee7c834f3ff1173c28cf6c2f1a3) )

#define ORLEG2_PROGRAM_101(prefix, extension) \
	ROM_REGION( 0x1000000, "user1", 0 ) \
	ROM_LOAD( #prefix "_v101" #extension ".u7",  0x000000, 0x800000, CRC(45805b53) SHA1(f2a8399c821b75fadc53e914f6f318707e70787c) )

/* 
   Internal ROMs for CHINA and OVERSEA are confirmed to differ by just the region byte, other regions not yet verified.
   label is a localized version of the game title and the country code (see above)
   For OVERSEA this is "O/L2", but we omit the / due to naming rules
   For the CHINA version this uses the Chinese characters
*/

#define ORLEG2_INTERNAL_CHINA \
	ROM_REGION( 0x04000, "maincpu", 0 ) \
	ROM_LOAD( "xyj2_cn.igs036", 0x00000000, 0x0004000, CRC(bcce7641) SHA1(c3b5cf6e9f6eae09b6785314777a52b34c3c7657) ) \
	ROM_REGION( 0x100, "default_card", 0 ) \
	ROM_LOAD( "blank_orleg2_china_card.pg2", 0x000, 0x100, CRC(099156f0) SHA1(a621c9772a98719c657bba3a1bf235487eb78615) )

#define ORLEG2_INTERNAL_OVERSEAS \
	ROM_REGION( 0x04000, "maincpu", 0 ) \
	ROM_LOAD( "ol2_fa.igs036", 0x00000000, 0x0004000, CRC(cc4d398a) SHA1(c50bcc81f02cd5aa8ad157d73209dc53bdedc023) )

ROM_START( orleg2 )
	ORLEG2_INTERNAL_OVERSEAS
	ORLEG2_PROGRAM_104(ol2,fa)
	ORLEG2_VIDEO_SOUND_ROMS
ROM_END

ROM_START( orleg2_103 )
	ORLEG2_INTERNAL_OVERSEAS
	ORLEG2_PROGRAM_103(ol2,fa)
	ORLEG2_VIDEO_SOUND_ROMS
ROM_END

ROM_START( orleg2_101 )
	ORLEG2_INTERNAL_OVERSEAS
	ORLEG2_PROGRAM_101(ol2,fa)
	ORLEG2_VIDEO_SOUND_ROMS
ROM_END

ROM_START( orleg2_104cn )
	ORLEG2_INTERNAL_CHINA
	ORLEG2_PROGRAM_104(xyj2,cn)
	ORLEG2_VIDEO_SOUND_ROMS
ROM_END

ROM_START( orleg2_103cn )
	ORLEG2_INTERNAL_CHINA
	ORLEG2_PROGRAM_103(xyj2,cn)
	ORLEG2_VIDEO_SOUND_ROMS
ROM_END

ROM_START( orleg2_101cn )
	ORLEG2_INTERNAL_CHINA
	ORLEG2_PROGRAM_101(xyj2,cn)
	ORLEG2_VIDEO_SOUND_ROMS
ROM_END

// Knights of Valour 2 New Legend

#define KOV2NL_VIDEO_SOUND_ROMS \
	ROM_REGION( 0x200000, "tiles", ROMREGION_ERASEFF ) \
	ROM_LOAD( "ig-a3_text.u4",           0x00000000, 0x0200000, CRC(214530ff) SHA1(4231a02054b0345392a077042b95779fd45d6c22) ) \
	\
	ROM_REGION( 0x1000000, "bgtile", 0 ) \
	ROM_LOAD32_WORD( "ig-a3_bgl.u35",    0x00000000, 0x0800000, CRC(2d46b1f6) SHA1(ea8c805eda6292e86a642e9633d8fee7054d10b1) ) \
	ROM_LOAD32_WORD( "ig-a3_bgh.u36",    0x00000002, 0x0800000, CRC(df710c36) SHA1(f826c3f496c4f17b46d18af1d8e02cac7b7027ac) ) \
	\
	ROM_REGION( 0x2000000, "sprites_mask", 0 ) /* 1bpp sprite mask data */ \
	ROM_LOAD32_WORD( "ig-a3_bml.u12",    0x00000000, 0x1000000, CRC(0bf63836) SHA1(b8e4f1951f8074b475b795bd7840c5a375b6f5ef) ) \
	ROM_LOAD32_WORD( "ig-a3_bmh.u16",    0x00000002, 0x1000000, CRC(4a378542) SHA1(5d06a8a8796285a786ebb690c34610f923ef5570) ) \
	\
	ROM_REGION( 0x4000000, "sprites_colour", 0 ) /* sprite colour data */ \
	ROM_LOAD32_WORD( "ig-a3_cgl.u18",    0x00000000, 0x2000000, CRC(8d923e1f) SHA1(14371cf385dd8857017d3111cd4710f4291b1ae2) ) \
	ROM_LOAD32_WORD( "ig-a3_cgh.u26",    0x00000002, 0x2000000, CRC(5b6fbf3f) SHA1(d1f52e230b91ee6cde939d7c2b74da7fd6527e73) ) \
	\
	ROM_REGION( 0x2000000, "ymz774", ROMREGION_ERASEFF ) /* ymz770 */ \
	ROM_LOAD16_WORD_SWAP( "ig-a3_sp.u37",            0x00000000, 0x2000000, CRC(45cdf422) SHA1(8005d284bcee73cff37a147fcd1c3e9f039a7203) ) \
	\
	ROM_REGION(0x10000, "sram", 0) \
	ROM_LOAD("gsyx_nvram", 0x00000000, 0x10000, CRC(22400c16) SHA1(f775a16299c30f2ce23d683161b910e06eff37c1) )

#define KOV2NL_PROGRAM_302 \
	ROM_REGION( 0x1000000, "user1", 0 ) \
	ROM_LOAD("gsyx_v302cn.u7", 0x00000000, 0x0800000, CRC(b19cf540) SHA1(25da5804bbfd7ef2cdf5cc5aabaa803d18b98929) )

#define KOV2NL_PROGRAM_301 \
	ROM_REGION( 0x1000000, "user1", 0 ) \
	ROM_LOAD("gsyx_v301cn.u7", 0x000000, 0x800000, CRC(c4595c2c) SHA1(09e379556ef76f81a63664f46d3f1415b315f384) )

#define KOV2NL_PROGRAM_300 \
	ROM_REGION( 0x1000000, "user1", 0 ) \
	ROM_LOAD("gsyx_v300tw.u7", 0x000000, 0x800000, CRC(08da7552) SHA1(303b97d7694405474c8133a259303ccb49db48b1) )

#define KOV2NL_INTERNAL_CHINA \
	ROM_REGION( 0x04000, "maincpu", 0 ) \
	ROM_LOAD( "gsyx_igs036_china.rom", 0x00000000, 0x0004000, CRC(e09fe4ce) SHA1(c0cac64ef8727cbe79d503ec4df66ddb6f2c925e) ) \
	ROM_REGION( 0x100, "default_card", 0 ) \
	ROM_LOAD( "blank_kov2nl_china_card.pg2", 0x000, 0x100, CRC(91786244) SHA1(ac0ce11b46c19ffe21f6b94bc83ef061f547b591) )


ROM_START( kov2nl )
	KOV2NL_INTERNAL_CHINA
	KOV2NL_PROGRAM_302
	KOV2NL_VIDEO_SOUND_ROMS
ROM_END

ROM_START( kov2nl_301 )
	KOV2NL_INTERNAL_CHINA
	KOV2NL_PROGRAM_301
	KOV2NL_VIDEO_SOUND_ROMS
ROM_END

ROM_START( kov2nl_300 )
	KOV2NL_INTERNAL_CHINA
	KOV2NL_PROGRAM_300
	KOV2NL_VIDEO_SOUND_ROMS
ROM_END

// Dodonpachi Daioujou Tamashii

#define DDPDOJH_VIDEO_SOUND_ROMS \
	ROM_REGION( 0x200000, "tiles", ROMREGION_ERASEFF ) \
	ROM_LOAD( "ddpdoj_text.u1",          0x00000000, 0x0200000, CRC(f18141d1) SHA1(a16e0a76bc926a158bb92dfd35aca749c569ef50) ) \
	\
	ROM_REGION( 0x2000000, "bgtile", 0 ) \
	ROM_LOAD32_WORD( "ddpdoj_bgl.u23",   0x00000000, 0x1000000, CRC(ff65fdab) SHA1(abdd5ca43599a2daa722547a999119123dd9bb28) ) \
	ROM_LOAD32_WORD( "ddpdoj_bgh.u24",   0x00000002, 0x1000000, CRC(bb84d2a6) SHA1(a576a729831b5946287fa8f0d923016f43a9bedb) ) \
	\
	ROM_REGION( 0x1000000, "sprites_mask", 0 ) /* 1bpp sprite mask data */ \
	ROM_LOAD32_WORD( "ddpdoj_mapl0.u13", 0x00000000, 0x800000, CRC(bcfbb0fc) SHA1(9ec478eba9905913cf997bd9b46c70c1ad383630) ) \
	ROM_LOAD32_WORD( "ddpdoj_maph0.u15", 0x00000002, 0x800000, CRC(0cc75d4e) SHA1(6d1b5ef0fdebf1e84fa199b939ffa07b810b12c9) ) \
	\
	ROM_REGION( 0x2000000, "sprites_colour", 0 ) /* sprite colour data */ \
	ROM_LOAD32_WORD( "ddpdoj_spa0.u9",   0x00000000, 0x1000000, CRC(1232c1b4) SHA1(ecc1c549ae19d2f052a85fe4a993608aedf49a25) ) \
	ROM_LOAD32_WORD( "ddpdoj_spb0.u18",  0x00000002, 0x1000000, CRC(6a9e2cbf) SHA1(8e0a4ea90f5ef534820303d62f0873f8ac9f080e) ) \
	\
	ROM_REGION( 0x1000000, "ymz774", ROMREGION_ERASEFF ) /* ymz770 */ \
	ROM_LOAD16_WORD_SWAP( "ddpdoj_wave0.u12",        0x00000000, 0x1000000, CRC(2b71a324) SHA1(f69076cc561f40ca564d804bc7bd455066f8d77c) )

ROM_START( ddpdojh )
	ROM_REGION( 0x04000, "maincpu", 0 )
	ROM_LOAD( "ddpdoj_igs036.rom",       0x00000000, 0x0004000, NO_DUMP ) // CRC(5db91464) SHA1(723d8086285805bd815e62120dfa9a4269bcd932)

	ROM_REGION( 0x1000000, "user1", 0 )
	ROM_LOAD( "ddpdoj_v201cn.u4",        0x00000000, 0x0200000, CRC(89e4b760) SHA1(9fad1309da31d12a413731b416a8bbfdb304ed9e) )

	DDPDOJH_VIDEO_SOUND_ROMS
ROM_END

// Knights of Valour 3

/*
   The Kov3 Program rom is a module consisting of a NOR flash and a FPGA, this provides an extra layer of encryption on top of the usual
   that is only unlocked when the correct sequence is recieved from the ARM MCU (IGS036)

   Newer gambling games use the same modules.
*/

#define KOV3_VIDEO_SOUND_ROMS \
	ROM_REGION( 0x200000, "tiles", ROMREGION_ERASEFF ) \
	ROM_LOAD( "kov3_text.u1",            0x00000000, 0x0200000, CRC(198b52d6) SHA1(e4502abe7ba01053d16c02114f0c88a3f52f6f40) ) \
	\
	ROM_REGION( 0x2000000, "bgtile", 0 ) \
	ROM_LOAD32_WORD( "kov3_bgl.u6",      0x00000000, 0x1000000, CRC(49a4c5bc) SHA1(26b7da91067bda196252520e9b4893361c2fc675) ) \
	ROM_LOAD32_WORD( "kov3_bgh.u7",      0x00000002, 0x1000000, CRC(adc1aff1) SHA1(b10490f0dbef9905cdb064168c529f0b5a2b28b8) ) \
	\
	ROM_REGION( 0x4000000, "sprites_mask", 0 ) /* 1bpp sprite mask data */ \
	ROM_LOAD32_WORD( "kov3_mapl0.u15",   0x00000000, 0x2000000, CRC(9e569bf7) SHA1(03d26e000e9d8e744546be9649628d2130f2ec4c) ) \
	ROM_LOAD32_WORD( "kov3_maph0.u16",   0x00000002, 0x2000000, CRC(6f200ad8) SHA1(cd12c136d4f5d424bd7daeeacd5c4127beb3d565) ) \
	\
	ROM_REGION( 0x8000000, "sprites_colour", 0 ) /* sprite colour data */ \
	ROM_LOAD32_WORD( "kov3_spa0.u17",    0x00000000, 0x4000000, CRC(3a1e58a9) SHA1(6ba251407c69ee62f7ea0baae91bc133acc70c6f) ) \
	ROM_LOAD32_WORD( "kov3_spb0.u10",    0x00000002, 0x4000000, CRC(90396065) SHA1(01bf9f69d77a792d5b39afbba70fbfa098e194f1) ) \
	\
	ROM_REGION( 0x4000000, "ymz774", ROMREGION_ERASEFF ) /* ymz770 */ \
	ROM_LOAD16_WORD_SWAP( "kov3_wave0.u13",              0x00000000, 0x4000000, CRC(aa639152) SHA1(2314c6bd05524525a31a2a4668a36a938b924ba4) )

ROM_START( kov3 )
	ROM_REGION( 0x04000, "maincpu", 0 )
	ROM_LOAD( "kov3_igs036.rom",         0x00000000, 0x0004000, NO_DUMP ) // CRC(c7d33764) SHA1(5cd48f876e637d60391d39ac6e40bf243300cc75)

	ROM_REGION( 0x1000000, "user1", 0 )
	ROM_LOAD( "kov3_v104cn_raw.bin",         0x00000000, 0x0800000, CRC(1b5cbd24) SHA1(6471d4842a08f404420dea2bd1c8b88798c80fd5) )

	KOV3_VIDEO_SOUND_ROMS
ROM_END

ROM_START( kov3_102 )
	ROM_REGION( 0x04000, "maincpu", 0 )
	ROM_LOAD( "kov3_igs036.rom",         0x00000000, 0x0004000, NO_DUMP )

	ROM_REGION( 0x1000000, "user1", 0 )
	ROM_LOAD( "kov3_v102cn_raw.bin",         0x00000000, 0x0800000, CRC(61d0dabd) SHA1(959b22ef4e342ca39c2386549ac7274f9d580ab8) )

	KOV3_VIDEO_SOUND_ROMS
ROM_END

ROM_START( kov3_100 )
	ROM_REGION( 0x04000, "maincpu", 0 )
	ROM_LOAD( "kov3_igs036.rom",         0x00000000, 0x0004000, NO_DUMP )

	ROM_REGION( 0x1000000, "user1", 0 )
	ROM_LOAD( "kov3_v100cn_raw.bin",         0x00000000, 0x0800000, CRC(93bca924) SHA1(ecaf2c4676eb3d9f5e4fdbd9388be41e51afa0e4) )

	KOV3_VIDEO_SOUND_ROMS
ROM_END

/* King of Fighters '98: Ultimate Match HERO

device types were as follows

kof98umh_v100cn.u4  SAMSUNG K8Q2815UQB
ig-d3_text.u1       cFeon EN29LV160AB
all others:         SPANSION S99-50070

*/

#define KOF98UMH_VIDEO_SOUND_ROMS \
	ROM_REGION( 0x200000, "tiles", ROMREGION_ERASEFF ) \
	ROM_LOAD( "ig-d3_text.u1",          0x00000000, 0x0200000, CRC(9a0ea82e) SHA1(7844fd7e46c3fbb2164060f160da528254fd177e) ) \
	\
	ROM_REGION( 0x2000000, "bgtile", ROMREGION_ERASEFF ) \
	/* bgl/bgh unpopulated (no background tilemap) */ \
	\
	ROM_REGION( 0x08000000, "sprites_mask", 0 ) /* 1bpp sprite mask data */ \
	ROM_LOAD32_WORD( "ig-d3_mapl0.u13", 0x00000000, 0x4000000, CRC(5571d63e) SHA1(dad73797a35738013d82e3b8ca96fa001ec56f69) ) \
	ROM_LOAD32_WORD( "ig-d3_maph0.u15", 0x00000002, 0x4000000, CRC(0da7b1b8) SHA1(87741242bd827eca3788b490df6dcb65f7a89733) ) \
	\
	ROM_REGION( 0x20000000, "sprites_colour", 0 ) /* sprite colour data - some byte are 0x40 or even 0xff, but verified on 2 boards */ \
	ROM_LOAD32_WORD( "ig-d3_spa0.u9",   0x00000000, 0x4000000, CRC(cfef8f7d) SHA1(54f58d1b9eb7d2e4bbe13fbdfd98f5b14ce2086b) ) \
	ROM_LOAD32_WORD( "ig-d3_spb0.u18",  0x00000002, 0x4000000, CRC(f199d5c8) SHA1(91f5e8efd1f6a9e5aada51afdf5a8f52bac24185) ) \
	/* spa1/spb1 unpopulated */ \
	ROM_LOAD32_WORD( "ig-d3_spa2.u10",  0x10000000, 0x4000000, CRC(03bfd35c) SHA1(814998cd5ee01c9da775b73f7a0ba4216fe4970e) ) \
	ROM_LOAD32_WORD( "ig-d3_spb2.u20",  0x10000002, 0x4000000, CRC(9aaa840b) SHA1(3c6078d53bb5eca5c501540214287dd102102ea1) ) \
	/* spa3/spb3 unpopulated */ \
	\
	ROM_REGION( 0x08000000, "ymz774", ROMREGION_ERASEFF ) /* ymz770 */ \
	ROM_LOAD16_WORD_SWAP( "ig-d3_wave0.u12",        0x00000000, 0x4000000, CRC(edf2332d) SHA1(7e01c7e03e515814d7de117c265c3668d32842fa) ) \
	ROM_LOAD16_WORD_SWAP( "ig-d3_wave1.u11",        0x04000000, 0x4000000, CRC(62321b20) SHA1(a388c8a2489430fbe92fb26b3ef81c66ce97f318) )

ROM_START( kof98umh )
	ROM_REGION( 0x04000, "maincpu", 0 )
	ROM_LOAD( "kof98uhm_igs036.rom",       0x00000000, 0x0004000, NO_DUMP ) // CRC(3ed2e50f) SHA1(35310045d375d9dda36c325e35257123a7b5b8c7)

	ROM_REGION( 0x1000000, "user1", 0 )
	ROM_LOAD( "kof98umh_v100cn.u4",        0x00000000, 0x1000000, CRC(2ea91e3b) SHA1(5a586bb99cc4f1b02e0db462d5aff721512e0640) )

	KOF98UMH_VIDEO_SOUND_ROMS
ROM_END

static void iga_u16_decode(uint16_t *rom, int len, int ixor)
{
	int i;

	for (i = 1; i < len / 2; i+=2)
	{
		uint16_t x = ixor;

		if ( (i>>1) & 0x000001) x ^= 0x0010;
		if ( (i>>1) & 0x000002) x ^= 0x2004;
		if ( (i>>1) & 0x000004) x ^= 0x0801;
		if ( (i>>1) & 0x000008) x ^= 0x0300;
		if ( (i>>1) & 0x000010) x ^= 0x0080;
		if ( (i>>1) & 0x000020) x ^= 0x0020;
		if ( (i>>1) & 0x000040) x ^= 0x4008;
		if ( (i>>1) & 0x000080) x ^= 0x1002;
		if ( (i>>1) & 0x000100) x ^= 0x0400;
		if ( (i>>1) & 0x000200) x ^= 0x0040;
		if ( (i>>1) & 0x000400) x ^= 0x8000;

		rom[i] ^= x;
		rom[i] = BITSWAP16(rom[i], 8,9,10,11,12,13,14,15,0,1,2,3,4,5,6,7);
	}
}

static void iga_u12_decode(uint16_t* rom, int len, int ixor)
{
	int i;

	for (i = 0; i < len / 2; i+=2)
	{
		uint16_t x = ixor;

		if ( (i>>1) & 0x000001) x ^= 0x9004;
		if ( (i>>1) & 0x000002) x ^= 0x0028;
		if ( (i>>1) & 0x000004) x ^= 0x0182;
		if ( (i>>1) & 0x000008) x ^= 0x0010;
		if ( (i>>1) & 0x000010) x ^= 0x2040;
		if ( (i>>1) & 0x000020) x ^= 0x0801;
		if ( (i>>1) & 0x000040) x ^= 0x0000;
		if ( (i>>1) & 0x000080) x ^= 0x0000;
		if ( (i>>1) & 0x000100) x ^= 0x4000;
		if ( (i>>1) & 0x000200) x ^= 0x0600;
		if ( (i>>1) & 0x000400) x ^= 0x0000;

		rom[i] ^= x;
		rom[i] = BITSWAP16(rom[i], 8,9,10,11,12,13,14,15,0,1,2,3,4,5,6,7);
	}
}

static void sprite_colour_decode(uint16_t* rom, int len)
{
	int i;

	for (i = 0; i < len / 2; i++)
	{
		rom[i] = BITSWAP16(rom[i], 15, 14, /* unused - 6bpp */
								   13, 12, 11,
								   5, 4, 3,
								   7, 6, /* unused - 6bpp */
								   10, 9, 8,
								   2, 1, 0  );
	}
}

READ32_MEMBER(pgm2_state::orleg2_speedup_r)
{
	int pc = space.device().safe_pc();
	if ((pc == 0x1002faec) || (pc == 0x1002f9b8))
	{
		if ((m_mainram[0x20114 / 4] == 0x00) && (m_mainram[0x20118 / 4] == 0x00))
			space.device().execute().spin_until_interrupt();
	}
	/*else
	{
	    printf("pc is %08x\n", pc);
	}*/

	return m_mainram[0x20114 / 4];
}

READ32_MEMBER(pgm2_state::kov2nl_speedup_r)
{
	int pc = space.device().safe_pc();

	if ((pc == 0x10053a94) || (pc == 0x1005332c) || (pc == 0x1005327c))
	{
		if ((m_mainram[0x20470 / 4] == 0x00) && (m_mainram[0x20474 / 4] == 0x00))
			space.device().execute().spin_until_interrupt();
	}
	/*
	else
	{
	    printf("pc is %08x\n", pc);
	}
	*/

	return m_mainram[0x20470 / 4];
}


// for games with the internal ROMs fully dumped that provide the sprite key and program rom key at runtime
void pgm2_state::common_encryption_init()
{
	uint16_t *src = (uint16_t *)memregion("sprites_mask")->base();

	iga_u12_decode(src, memregion("sprites_mask")->bytes(), 0x0000);
	iga_u16_decode(src, memregion("sprites_mask")->bytes(), 0x0000);
	m_sprite_predecrypted = 0;

	src = (uint16_t *)memregion("sprites_colour")->base();
	sprite_colour_decode(src, memregion("sprites_colour")->bytes());

	m_has_decrypted = 0;
}

DRIVER_INIT_MEMBER(pgm2_state,orleg2)
{
	common_encryption_init();
	machine().device("maincpu")->memory().space(AS_PROGRAM).install_read_handler(0x20020114, 0x20020117, read32_delegate(FUNC(pgm2_state::orleg2_speedup_r),this));
}

DRIVER_INIT_MEMBER(pgm2_state,kov2nl)
{
	common_encryption_init();
	machine().device("maincpu")->memory().space(AS_PROGRAM).install_read_handler(0x20020470, 0x20020473, read32_delegate(FUNC(pgm2_state::kov2nl_speedup_r), this));
}

DRIVER_INIT_MEMBER(pgm2_state,ddpdojh)
{
	uint16_t *src = (uint16_t *)memregion("sprites_mask")->base();

	iga_u12_decode(src, 0x1000000, 0x1e96);
	iga_u16_decode(src, 0x1000000, 0x869c);
	m_sprite_predecrypted = 1;

	src = (uint16_t *)memregion("sprites_colour")->base();
	sprite_colour_decode(src, 0x2000000);

	igs036_decryptor decrypter(ddpdoj_key);
	decrypter.decrypter_rom(memregion("user1"));
	m_has_decrypted = 1;
}

DRIVER_INIT_MEMBER(pgm2_state,kov3)
{
	uint16_t *src = (uint16_t *)memregion("sprites_mask")->base();

	iga_u12_decode(src, 0x4000000, 0x956d);
	iga_u16_decode(src, 0x4000000, 0x3d17);
	m_sprite_predecrypted = 1;

	src = (uint16_t *)memregion("sprites_colour")->base();
	sprite_colour_decode(src, 0x8000000);

	igs036_decryptor decrypter(kov3_key);
	decrypter.decrypter_rom(memregion("user1"));
	m_has_decrypted = 1;
}

void pgm2_state::decrypt_kov3_module(uint32_t addrxor, uint16_t dataxor)
{
	uint16_t *src = (uint16_t *)memregion("user1")->base();
	uint32_t size = memregion("user1")->bytes();

	std::vector<uint16_t> buffer(size/2);

	for (int i = 0; i < size/2; i++)
		buffer[i] = src[i^addrxor]^dataxor;

	memcpy(src, &buffer[0], size);
}

DRIVER_INIT_MEMBER(pgm2_state, kov3_104)
{
	decrypt_kov3_module(0x18ec71, 0xb89d);
	DRIVER_INIT_CALL(kov3);
}

DRIVER_INIT_MEMBER(pgm2_state, kov3_102)
{
	decrypt_kov3_module(0x021d37, 0x81d0);
	DRIVER_INIT_CALL(kov3);
}

DRIVER_INIT_MEMBER(pgm2_state, kov3_100)
{
	decrypt_kov3_module(0x3e8aa8, 0xc530);
	DRIVER_INIT_CALL(kov3);
}

DRIVER_INIT_MEMBER(pgm2_state,kof98umh)
{
	uint16_t *src = (uint16_t *)memregion("sprites_mask")->base();

	iga_u12_decode(src, 0x08000000, 0x21df);
	iga_u16_decode(src, 0x08000000, 0x8692);
	m_sprite_predecrypted = 1;

	src = (uint16_t *)memregion("sprites_colour")->base();
	sprite_colour_decode(src, 0x20000000);

	igs036_decryptor decrypter(kof98umh_key);
	decrypter.decrypter_rom(memregion("user1"));
	m_has_decrypted = 1;
}

/* PGM2 */

// Oriental Legend 2 - should be a V102 and V100 too
GAME( 2007, orleg2,       0,         pgm2,    pgm2, pgm2_state,     orleg2,       ROT0, "IGS", "Oriental Legend 2 (V104, Oversea)", 0 ) /* Overseas sets of OL2 do not use the card reader */
GAME( 2007, orleg2_103,   orleg2,    pgm2,    pgm2, pgm2_state,     orleg2,       ROT0, "IGS", "Oriental Legend 2 (V103, Oversea)", 0 )
GAME( 2007, orleg2_101,   orleg2,    pgm2,    pgm2, pgm2_state,     orleg2,       ROT0, "IGS", "Oriental Legend 2 (V101, Oversea)", 0 )

GAME( 2007, orleg2_104cn, orleg2,    pgm2,    pgm2, pgm2_state,     orleg2,       ROT0, "IGS", "Oriental Legend 2 (V104, China)", 0 )
GAME( 2007, orleg2_103cn, orleg2,    pgm2,    pgm2, pgm2_state,     orleg2,       ROT0, "IGS", "Oriental Legend 2 (V103, China)", 0 )
GAME( 2007, orleg2_101cn, orleg2,    pgm2,    pgm2, pgm2_state,     orleg2,       ROT0, "IGS", "Oriental Legend 2 (V101, China)", 0 )

// Knights of Valour 2 New Legend
GAME( 2008, kov2nl,       0,         pgm2,    pgm2, pgm2_state,     kov2nl,       ROT0, "IGS", "Knights of Valour 2 New Legend (V302, China)", 0 )
GAME( 2008, kov2nl_301,   kov2nl,    pgm2,    pgm2, pgm2_state,     kov2nl,       ROT0, "IGS", "Knights of Valour 2 New Legend (V301, China)", 0 )
GAME( 2008, kov2nl_300,   kov2nl,    pgm2,    pgm2, pgm2_state,     kov2nl,       ROT0, "IGS", "Knights of Valour 2 New Legend (V300, China)", 0 ) // was dumped from a Taiwan board tho

// Dodonpachi Daioujou Tamashii - should be a V200 too
GAME( 2010, ddpdojh,      0,    pgm2,    pgm2, pgm2_state,     ddpdojh,    ROT270, "IGS", "Dodonpachi Daioujou Tamashii (V201, China)", MACHINE_NOT_WORKING )

// Knights of Valour 3 - should be a V103 and V101 too
GAME( 2011, kov3,         0,    pgm2,    pgm2, pgm2_state,     kov3_104,   ROT0, "IGS", "Knights of Valour 3 (V104, China)", MACHINE_NOT_WORKING )
GAME( 2011, kov3_102,     kov3, pgm2,    pgm2, pgm2_state,     kov3_102,   ROT0, "IGS", "Knights of Valour 3 (V102, China)", MACHINE_NOT_WORKING )
GAME( 2011, kov3_100,     kov3, pgm2,    pgm2, pgm2_state,     kov3_100,   ROT0, "IGS", "Knights of Valour 3 (V100, China)", MACHINE_NOT_WORKING )

// King of Fighters '98: Ultimate Match Hero
GAME( 2009, kof98umh,     0,    pgm2,    pgm2, pgm2_state,     kof98umh,   ROT0, "IGS / SNK Playmore / NewChannel", "The King of Fighters '98: Ultimate Match HERO (China, V100, 09-08-23)", MACHINE_NOT_WORKING )

// Jigsaw World Arena

// Puzzle of Ocha / Ochainu No Pazuru


