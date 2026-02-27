TARGET = chexquest

COMMON_SRC = \
	am_map.c \
	d_event.c \
	d_items.c \
	d_iwad.c \
	d_loop.c \
	d_main.c \
	d_mode.c \
	d_net.c \
	deh_str.c \
	doomdef.c \
	doomgeneric.c \
	doomstat.c \
	dstrings.c \
	dummy.c \
	f_finale.c \
	f_wipe.c \
	g_game.c \
	gusconf.c \
	hu_lib.c \
	hu_stuff.c \
	i_cdmus.c \
	i_endoom.c \
	i_input.c \
	i_joystick.c \
	i_scale.c \
	i_sound.c \
	i_system.c \
	i_timer.c \
	i_video.c \
	icon.c \
	info.c \
	m_argv.c \
	m_bbox.c \
	m_cheat.c \
	m_config.c \
	m_controls.c \
	m_fixed.c \
	m_menu.c \
	m_misc.c \
	m_random.c \
	memio.c \
	mus2mid.c \
	p_ceilng.c \
	p_doors.c \
	p_enemy.c \
	p_floor.c \
	p_inter.c \
	p_lights.c \
	p_map.c \
	p_maputl.c \
	p_mobj.c \
	p_plats.c \
	p_pspr.c \
	p_saveg.c \
	p_setup.c \
	p_sight.c \
	p_spec.c \
	p_switch.c \
	p_telept.c \
	p_tick.c \
	p_user.c \
	r_bsp.c \
	r_data.c \
	r_draw.c \
	r_main.c \
	r_plane.c \
	r_segs.c \
	r_sky.c \
	r_things.c \
	s_sound.c \
	sha1.c \
	sounds.c \
	st_lib.c \
	st_stuff.c \
	statdump.c \
	tables.c \
	v_video.c \
	w_checksum.c \
	w_file.c \
	w_file_stdc.c \
	w_main.c \
	w_wad.c \
	wi_stuff.c \
	z_zone.c

OBJS = $(COMMON_SRC:.c=.o) doomgeneric_psp.o

INCDIR =
CFLAGS = -std=c99 -O2 -G0 -Wall -DPSP -DNORMALUNIX -DFEATURE_SOUND=0
CFLAGS += -DDOOMGENERIC_RESX=480 -DDOOMGENERIC_RESY=272
CFLAGS += -Wno-unused-variable -Wno-unused-parameter -Wno-pointer-sign
CFLAGS += -Wno-missing-braces -Wno-implicit-function-declaration
CFLAGS += -Wno-char-subscripts -Wno-sign-compare
CXXFLAGS = $(CFLAGS)
ASFLAGS = $(CFLAGS)

LIBDIR =
LDFLAGS =
LIBS = -lpspgu -lpspgum -lpspdisplay -lpspge -lpspctrl \
       -lpspaudio -lpspaudiolib -lpsppower -lpsprtc -lm

BUILD_PRX = 1
PSP_FW_VERSION = 660

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Chex Quest PSP
PSP_EBOOT_ICON = NULL

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
