#ifndef LSOUNDS_H
#define LSOUNDS_H
#ifdef _WIN32
#pragma once
#endif

/*
** Ported from Experiment: Source (src/public/lsounds.h).
**
** Upstream this header also declares the AudioChannel userdata, which wraps
** IAudioChannel from their util/bassmanager -- a binding layer over the BASS audio
** library for streaming URLs and files (Sounds.PlayUrl / Sounds.PlayFile).  HL2SB
** does not ship BASS or that manager, so only the Sounds library was ported; see
** public/lsounds.cpp for what was kept and what was dropped.
*/

#endif  // LSOUNDS_H
