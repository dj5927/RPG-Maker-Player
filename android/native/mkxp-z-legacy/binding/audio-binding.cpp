/*
** audio-binding.cpp
**
** This file is part of mkxp.
**
** Copyright (C) 2013 - 2021 Amaryllis Kulla <ancurio@mapleshrine.eu>
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** mkxp is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "audio.h"
#include "sharedstate.h"
#include "binding-util.h"
#include "exception.h"

#define DEF_PLAY_STOP_POS(entity) \
	RB_METHOD_GUARD(audio_##entity##Play) \
	{ \
		RB_UNUSED_PARAM; \
		const char *filename; \
		int volume = 100; \
		int pitch = 100; \
		double pos = 0.0; \
		rb_get_args(argc, argv, "z|iif", &filename, &volume, &pitch, &pos RB_ARG_END); \
		shState->audio().entity##Play(filename, volume, pitch, pos); \
		return Qnil; \
	} \
	RB_METHOD_GUARD_END \
	RB_METHOD(audio_##entity##Stop) \
	{ \
		RB_UNUSED_PARAM; \
		shState->audio().entity##Stop(); \
		return Qnil; \
	} \
	RB_METHOD(audio_##entity##Pos) \
	{ \
		RB_UNUSED_PARAM; \
		return rb_float_new(shState->audio().entity##Pos()); \
	}

#define DEF_PLAY_STOP(entity) \
	RB_METHOD_GUARD(audio_##entity##Play) \
	{ \
		RB_UNUSED_PARAM; \
		const char *filename; \
		int volume = 100; \
		int pitch = 100; \
		rb_get_args(argc, argv, "z|ii", &filename, &volume, &pitch RB_ARG_END); \
		shState->audio().entity##Play(filename, volume, pitch); \
		return Qnil; \
	} \
	RB_METHOD_GUARD_END \
	RB_METHOD(audio_##entity##Stop) \
	{ \
		RB_UNUSED_PARAM; \
		shState->audio().entity##Stop(); \
		return Qnil; \
	}

#define DEF_FADE(entity) \
RB_METHOD(audio_##entity##Fade) \
{ \
	RB_UNUSED_PARAM; \
	int time; \
	rb_get_args(argc, argv, "i", &time RB_ARG_END); \
	shState->audio().entity##Fade(time); \
	return Qnil; \
}

#define DEF_POS(entity) \
	RB_METHOD(audio_##entity##Pos) \
	{ \
		RB_UNUSED_PARAM; \
		return rb_float_new(shState->audio().entity##Pos()); \
	}

// DEF_PLAY_STOP_POS( bgm )

#define MAYBE_NIL_TRACK(t) t == Qnil ? -127 : NUM2INT(t)

RB_METHOD_GUARD(audio_bgmPlay)
{
    RB_UNUSED_PARAM;
    const char *filename;
    int volume = 100;
    int pitch = 100;
    double pos = 0.0;
    VALUE track = Qnil;
    rb_get_args(argc, argv, "z|iifo", &filename, &volume, &pitch, &pos, &track RB_ARG_END);
    shState->audio().bgmPlay(filename, volume, pitch, pos, MAYBE_NIL_TRACK(track));
    return Qnil;
}
RB_METHOD_GUARD_END

RB_METHOD(audio_bgmStop)
{
    RB_UNUSED_PARAM;
    VALUE track = Qnil;
    rb_get_args(argc, argv, "|o", &track RB_ARG_END);
    shState->audio().bgmStop(MAYBE_NIL_TRACK(track));
    return Qnil;
}

RB_METHOD(audio_bgmPos)
{
    RB_UNUSED_PARAM;
    VALUE track = Qnil;
    rb_get_args(argc, argv, "|o", &track RB_ARG_END);
    return rb_float_new(shState->audio().bgmPos(MAYBE_NIL_TRACK(track)));
}

RB_METHOD_GUARD(audio_bgmGetVolume)
{
    RB_UNUSED_PARAM;
    VALUE track = Qnil;
    rb_get_args(argc, argv, "|o", &track RB_ARG_END);
    int ret = 0;
    ret = shState->audio().bgmGetVolume(MAYBE_NIL_TRACK(track));
    return rb_fix_new(ret);
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(audio_bgmSetVolume)
{
    RB_UNUSED_PARAM;
    int volume;
    VALUE track = Qnil;
    rb_get_args(argc, argv, "i|o", &volume, &track RB_ARG_END);
    shState->audio().bgmSetVolume(volume, MAYBE_NIL_TRACK(track));
    return Qnil;
}
RB_METHOD_GUARD_END

DEF_PLAY_STOP_POS( bgs )

DEF_PLAY_STOP( me )

//DEF_FADE( bgm )
RB_METHOD(audio_bgmFade)
{
    RB_UNUSED_PARAM;
    int time;
    VALUE track = Qnil;
    rb_get_args(argc, argv, "i|o", &time, &track RB_ARG_END);
    shState->audio().bgmFade(time, MAYBE_NIL_TRACK(track));
    return Qnil;
}

DEF_FADE( bgs )
DEF_FADE( me )

DEF_PLAY_STOP( se )

RB_METHOD(audioSetupMidi)
{
	RB_UNUSED_PARAM;

	shState->audio().setupMidi();

	return Qnil;
}

RB_METHOD(audioReset)
{
	RB_UNUSED_PARAM;

	shState->audio().reset();

	return Qnil;
}

// RPGMP RGSS native-compat layer.
// The public GPL android-mkxp implementation exposed a small FmodEx module
// by forwarding it to the normal RGSS Audio backend.  Keep the same idea
// here, but accept the wider FmodEx call signature and ignore unsupported
// streaming/memory/DLS tail arguments instead of raising ArgumentError.
RB_METHOD(fmodexInit)
{
    RB_UNUSED_PARAM;
    return Qtrue;
}

RB_METHOD(fmodexSetLoopPoints)
{
    RB_UNUSED_PARAM;
    return Qnil;
}

RB_METHOD(fmodexNoSound)
{
    RB_UNUSED_PARAM;
    return Qnil;
}

RB_METHOD_GUARD(fmodexBgmPlay)
{
    if (argc < 1)
        rb_raise(rb_eArgError, "filename required");
    VALUE filenameObj = rb_obj_as_string(argv[0]);
    const char *filename = RSTRING_PTR(filenameObj);
    int volume = (argc > 1 && argv[1] != Qnil) ? NUM2INT(argv[1]) : 100;
    int pitch = (argc > 2 && argv[2] != Qnil) ? NUM2INT(argv[2]) : 100;
    shState->audio().bgmPlay(filename, volume, pitch, 0.0, -127);
    return Qnil;
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(fmodexBgsPlay)
{
    if (argc < 1)
        rb_raise(rb_eArgError, "filename required");
    VALUE filenameObj = rb_obj_as_string(argv[0]);
    const char *filename = RSTRING_PTR(filenameObj);
    int volume = (argc > 1 && argv[1] != Qnil) ? NUM2INT(argv[1]) : 100;
    int pitch = (argc > 2 && argv[2] != Qnil) ? NUM2INT(argv[2]) : 100;
    shState->audio().bgsPlay(filename, volume, pitch, 0.0);
    return Qnil;
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(fmodexMePlay)
{
    if (argc < 1)
        rb_raise(rb_eArgError, "filename required");
    VALUE filenameObj = rb_obj_as_string(argv[0]);
    const char *filename = RSTRING_PTR(filenameObj);
    int volume = (argc > 1 && argv[1] != Qnil) ? NUM2INT(argv[1]) : 100;
    int pitch = (argc > 2 && argv[2] != Qnil) ? NUM2INT(argv[2]) : 100;
    shState->audio().mePlay(filename, volume, pitch);
    return Qnil;
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(fmodexSePlay)
{
    if (argc < 1)
        rb_raise(rb_eArgError, "filename required");
    VALUE filenameObj = rb_obj_as_string(argv[0]);
    const char *filename = RSTRING_PTR(filenameObj);
    int volume = (argc > 1 && argv[1] != Qnil) ? NUM2INT(argv[1]) : 100;
    int pitch = (argc > 2 && argv[2] != Qnil) ? NUM2INT(argv[2]) : 100;
    shState->audio().sePlay(filename, volume, pitch);
    return Qnil;
}
RB_METHOD_GUARD_END


#define BIND_PLAY_STOP(entity) \
	_rb_define_module_function(module, #entity "_play", audio_##entity##Play); \
	_rb_define_module_function(module, #entity "_stop", audio_##entity##Stop);

#define BIND_FADE(entity) \
	_rb_define_module_function(module, #entity "_fade", audio_##entity##Fade);

#define BIND_PLAY_STOP_FADE(entity) \
	BIND_PLAY_STOP(entity) \
	BIND_FADE(entity)

#define BIND_POS(entity) \
	_rb_define_module_function(module, #entity "_pos", audio_##entity##Pos);


void
audioBindingInit()
{
	VALUE module = rb_define_module("Audio");
	VALUE fmodex = rb_define_module("FmodEx");
	rb_define_class_under(fmodex, "Error", rb_eStandardError);

	BIND_PLAY_STOP_FADE( bgm );
    _rb_define_module_function(module, "bgm_volume", audio_bgmGetVolume);
    _rb_define_module_function(module, "bgm_set_volume", audio_bgmSetVolume);
	BIND_PLAY_STOP_FADE( bgs );
	BIND_PLAY_STOP_FADE( me  );

	BIND_POS( bgm );
	BIND_POS( bgs );

	_rb_define_module_function(module, "setup_midi", audioSetupMidi);

	BIND_PLAY_STOP( se )

	_rb_define_module_function(module, "__reset__", audioReset);

	_rb_define_module_function(fmodex, "init", fmodexInit);
	_rb_define_module_function(fmodex, "set_loop_points", fmodexSetLoopPoints);
	_rb_define_module_function(fmodex, "bgm_play", fmodexBgmPlay);
	_rb_define_module_function(fmodex, "bgm_stop", audio_bgmStop);
	_rb_define_module_function(fmodex, "bgm_fade", audio_bgmFade);
	_rb_define_module_function(fmodex, "bgm_pos", audio_bgmPos);
	_rb_define_module_function(fmodex, "bgm_sound", fmodexNoSound);
	_rb_define_module_function(fmodex, "bgs_play", fmodexBgsPlay);
	_rb_define_module_function(fmodex, "bgs_stop", audio_bgsStop);
	_rb_define_module_function(fmodex, "bgs_fade", audio_bgsFade);
	_rb_define_module_function(fmodex, "bgs_pos", audio_bgsPos);
	_rb_define_module_function(fmodex, "bgs_sound", fmodexNoSound);
	_rb_define_module_function(fmodex, "me_play", fmodexMePlay);
	_rb_define_module_function(fmodex, "me_stop", audio_meStop);
	_rb_define_module_function(fmodex, "me_fade", audio_meFade);
	_rb_define_module_function(fmodex, "fade_out", audio_meFade);
	_rb_define_module_function(fmodex, "me_sound", fmodexNoSound);
	_rb_define_module_function(fmodex, "se_play", fmodexSePlay);
	_rb_define_module_function(fmodex, "se_stop", audio_seStop);
}
