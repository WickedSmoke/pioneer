// Faun sound backend
// Copyright © 2024 Karl Robillard
// Licensed under the terms of the GPL v3. See licenses/GPL-3.txt

#include "Sound.h"
#include "Body.h"
#include "FileSystem.h"
#include "JobQueue.h"
#include "Pi.h"
#include "Player.h"
#include "utils.h"

#include <faun.h>

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/stat.h>
#endif

namespace Sound {

	static const int BUF_COUNT = 196;
	static const int SRC_COUNT = 10;
	static const int STREAM_COUNT = 6;	// (Faun max is 6)

	// The first two streams are for music.
	static const int STREAM0 = SRC_COUNT;
	static const int STREAM_FX = STREAM0+2;
	static const int MAX_SI = SRC_COUNT + STREAM_COUNT;

	// sounds/Ship/Thruster_large.ogg is 85787 bytes (22KHz, 7.98 seconds)
	static const int64_t STREAM_IF_LONGER_THAN = 88000;
	//static const float STREAM_IF_LONGER_THAN = 10.0;

	static float m_masterVol = 1.0f;
	static float m_sfxVol = 1.0f;

	void SetMasterVolume(const float vol)
	{
		m_masterVol = vol;
	}

	float GetMasterVolume()
	{
		return m_masterVol;
	}

	void SetSfxVolume(const float vol)
	{
		m_sfxVol = vol;
	}

	float GetSfxVolume()
	{
		return m_sfxVol;
	}

	void CalculateStereo(const Body *b, float vol, float *volLeftOut, float *volRightOut)
	{
		vector3d pos(0.0);

		if (b != Pi::player) {
			pos = b->GetPositionRelTo(Pi::player);
			pos = pos * Pi::player->GetOrient();
		}

		const float len = pos.Length();
		if (!is_zero_general(len)) {
			vol = vol / (0.002 * len);
			double dot = pos.Normalized().x * vol;

			(*volLeftOut) = vol * (2.0f - (1.0 + dot));
			(*volRightOut) = vol * (1.0 + dot);
		} else {
			(*volLeftOut) = (*volRightOut) = vol;
		}
		(*volLeftOut) = Clamp((*volLeftOut), 0.0f, 1.0f);
		(*volRightOut) = Clamp((*volRightOut), 0.0f, 1.0f);
	}

	void BodyMakeNoise(const Body *b, const char *sfx, float vol)
	{
		float vl, vr;
		CalculateStereo(b, vol, &vl, &vr);
		Sound::PlaySfx(sfx, vl, vr, 0);
	}

	struct Sample {
		std::string path;
		int bufN;			// If -1 then path is an ogg we must stream.
		float duration;
		bool isMusic;
	};

	typedef uint32_t eventid;

	struct SoundEvent {
		const Sample *sample = nullptr;
		eventid identifier;
	};

	static std::map<std::string, Sample> sfx_samples;
	struct SoundEvent wavstream[MAX_SI];

	static Sample *GetSample(const char *filename)
	{
		if (sfx_samples.find(filename) != sfx_samples.end()) {
			return &sfx_samples[filename];
		} else {
			//SilentWarning("Unknown sound sample: %s", filename);
			return 0;
		}
	}

	static SoundEvent *GetEvent(eventid id)
	{
		int si = FAUN_PID_SOURCE(id);
		if (wavstream[si].sample && (wavstream[si].identifier == id))
			return &wavstream[si];
		return nullptr;
	}

	static inline void DestroyEvent(SoundEvent *ev)
	{
		ev->sample = nullptr;
		ev->identifier = 0;
	}

	static uint32_t nextSource = 0;
	static uint32_t nextStream = 0;

	static eventid PlaySfxSample(Sample *sample, const float volume_left, const float volume_right, const Op op)
	{
		eventid id;
		int si;
		int mode = (op & Sound::OP_REPEAT) ? FAUN_PLAY_LOOP : FAUN_PLAY_ONCE;

		if (sample->bufN >= 0) {
			si = nextSource;
			if (++nextSource == SRC_COUNT)
				nextSource = 0;

			//printf("KR Sfx %d %s\n", op, sample->path.c_str());
			id = faun_playSourceVol(si, sample->bufN, mode,
									volume_left, volume_right);
		} else {
			si = nextStream;
			if (++nextStream == 4)
				nextStream = 0;

			char dpath[80];
			sprintf(dpath, "data/%s", sample->path.c_str());
			//printf("KR SfxStream %d %s\n", op, dpath);

			si += STREAM_FX;
			faun_setParameter(si, 1, FAUN_VOLUME, volume_left);
			id = faun_playStream(si, dpath, 0, 0, mode);
		}

		wavstream[si].sample = id ? sample : nullptr;
		wavstream[si].identifier = id;
		//printf("KR     eid %x\n", id);
		return id;
	}

	void PlaySfx(const char *fx, const float volume_left, const float volume_right, const Op op)
	{
		//printf("KR PlaySfx %s %f,%f %d\n", fx, volume_left, volume_right, op);
		Sample* sample = GetSample(fx);
		if (sample) {
			PlaySfxSample(sample, volume_left, volume_right, op);
		}
	}

	void DestroyAllEvents()
	{
		/* silence any sound events */
		faun_control(0, MAX_SI, FC_STOP);

		for (int si = 0; si < MAX_SI; si++) {
			DestroyEvent(&wavstream[si]);
		}
	}

	void DestroyAllEventsExceptMusic()
	{
		/* silence any sound events EXCEPT music
		   which are on STREAM0 and STREAM0+1 */
		const int stream1 = STREAM0+1;

		faun_control(0, SRC_COUNT, FC_STOP);
		faun_control(STREAM0+2, STREAM_COUNT-2, FC_STOP);

		for (int si = 0; si < MAX_SI; si++) {
			if (si < STREAM0 || si > stream1)
				DestroyEvent(&wavstream[si]);
		}
	}

	static int64_t fileSize(const char* path)
	{
#ifdef _WIN32
		WIN32_FIND_DATA data;
		HANDLE fh = FindFirstFile(path, &data);
		if (fh == INVALID_HANDLE_VALUE)
			return -1;
		FindClose(fh);

		int64_t size = data.nFileSizeLow;
		if (data.nFileSizeHigh)
			size += data.nFileSizeHigh * (((int64_t) MAXDWORD)+1);
		return size;
#else
		struct stat buf;
		return (stat(path, &buf) == -1) ? -1 : buf.st_size;
#endif
	}

	static uint32_t faunBufCount = 0;
	static std::pair<std::string, Sample> load_sound(const std::string &basename, const std::string &path, bool is_music)
	{
		PROFILE_SCOPED()
		if (!ends_with_ci(basename, ".ogg")) return {};

		Sample sample;
		sample.path = path;
		sample.bufN = -1;
		sample.duration = 0.0f;
		sample.isMusic = is_music;

		if (is_music) {
			printf("KR load_music %s %s\n", basename.c_str(), path.c_str());

			// music keyed by pathname minus (datapath)/music/ and extension
			return { path.substr(0, path.size() - 4), sample };
		} else {
			char dpath[80];
			sprintf(dpath, "data/%s", path.c_str());

			// Load into buffer if short enough.  File size is used as an
			// approximation of duration.
			int64_t fsize = fileSize(dpath);
			if (fsize <= STREAM_IF_LONGER_THAN) {
				sample.bufN = faunBufCount++;
				assert(sample.bufN < (int) BUF_COUNT);
				sample.duration = faun_loadBuffer(sample.bufN, dpath, 0, 0);
			}

			// KR load_sound airflow01.ogg sounds/Ship/airflow01.ogg 0
			printf("KR load_sound %s %s %d:%f\n",
				basename.c_str(), path.c_str(), sample.bufN, sample.duration);

			// sfx keyed by basename minus the .ogg
			return { basename.substr(0, basename.size() - 4), sample };
		}
	}

	class LoadSoundJob : public Job {
	public:
		LoadSoundJob(const std::string &directory, bool isMusic) :
			m_directory(directory),
			m_isMusic(isMusic)
		{}

		virtual void OnRun() override
		{
			PROFILE_SCOPED()
			// TODO: this is *probably* thread-safe, but FileSystem could use some further evaluation
			for (FileSystem::FileEnumerator files(FileSystem::gameDataFiles, m_directory, FileSystem::FileEnumerator::Recurse); !files.Finished(); files.Next()) {
				const FileSystem::FileInfo &info = files.Current();
				assert(info.IsFile());
				std::pair<std::string, Sample> result = load_sound(info.GetName(), info.GetPath(), m_isMusic);
				m_loadedSounds.emplace(result);
			}
		}

		virtual void OnFinish() override
		{
			for (const auto &pair : m_loadedSounds) {
				sfx_samples.emplace(std::move(pair));
			}
		}

	private:
		std::string m_directory;
		bool m_isMusic;
		std::map<std::string, Sample> m_loadedSounds;
	};

	bool Init(bool /*automaticallyOpenDevice*/)
	{
		PROFILE_SCOPED()
		faun_startup(BUF_COUNT, SRC_COUNT, STREAM_COUNT, 1, "pioneer");

		// load all the wretched effects
		Pi::GetApp()->GetAsyncStartupQueue()->Order(new LoadSoundJob("sounds", false));

		//I'd rather do this in MusicPlayer and store in a different map too, this will do for now
		Pi::GetApp()->GetAsyncStartupQueue()->Order(new LoadSoundJob("music", true));

		/* silence any sound events */
		DestroyAllEvents();
		return true;
	}

	bool InitDevice(std::string & /*name*/)
	{
		DestroyAllEvents();
		return true;
	}

	void Uninit()
	{
		faun_shutdown();
	}

	void UpdateAudioDevices()
	{
	}

	void Pause(int on)
	{
		faun_suspend(on);
	}

	void Event::Play(const char *fx, float volume_left, float volume_right, Op op)
	{
		//printf("KR Play %s %f,%f %d\n", fx, volume_left, volume_right, op);
		Stop();
		Sample* sample = GetSample(fx);
		if (sample) {
			eid = PlaySfxSample(sample, volume_left, volume_right, op);
		}
	}

	static int nextMusicStream = 0;
	static float musicFadeDelta = 0.0f;

	void Event::PlayMusic(const char *fx, float volume, float fadeDelta, bool repeat, Event* fadeOut)
	{
		//printf("KR PlayMusic %s %f %f %d\n", fx, volume, fadeDelta, repeat);
#if 1
		if (fadeDelta && musicFadeDelta != fadeDelta) {
			musicFadeDelta = fadeDelta;
			faun_setParameter(STREAM0, 2, FAUN_FADE_PERIOD, 1.0f/fadeDelta);
		}

		if (fadeOut && fadeOut->eid)
			faun_control(FAUN_PID_SOURCE(fadeOut->eid), 1, FC_FADE_OUT);

		Sample* sample = GetSample(fx);
		if (sample) {
			const int si = STREAM0 + nextMusicStream;
			nextMusicStream ^= 1;

			int mode = repeat ? FAUN_PLAY_LOOP : FAUN_PLAY_ONCE;
			if (fadeDelta)
				mode |= FAUN_PLAY_FADE_IN;

			char dpath[80];
			sprintf(dpath, "data/%s.ogg", fx);

			faun_setParameter(si, 1, FAUN_VOLUME, volume);
			eid = faun_playStream(si, dpath, 0, 0, mode);

			wavstream[si].sample = sample;
			wavstream[si].identifier = eid;
		}
#else
		static uint8_t crossfade[] = {
			FO_SOURCE, STREAM0, FO_FADE_OUT, FO_SOURCE, STREAM0+1,
			FO_SET_FADE, 10, FO_SET_VOL, 255,
			FO_START_STREAM, FAUN_PLAY_ONCE | FAUN_PLAY_FADE_IN,
			FO_END
		};

		Sample* sample = GetSample(fx);
		if (sample) {
			const int si = STREAM0 + nextMusicStream;
			nextMusicStream ^= 1;

			char dpath[80];
			sprintf(dpath, "data/%s.ogg", fx);
			eid = faun_playStream(si, dpath, 0, 0, 0);

			wavstream[si].sample = eid ? sample : 0;
			wavstream[si].identifier = eid;

			if (eid) {
				uint8_t* prog = crossfade;
				assert(prog[9] == FO_START_STREAM);

				prog[1] = STREAM0 + nextMusicStream;
				prog[4] = si;
				prog[6] = uint8_t(10.0f / fadeDelta);
				prog[8] = uint8_t(volume * 255.0f);
				prog[10] = repeat ? FAUN_PLAY_LOOP | FAUN_PLAY_FADE_IN
								  : FAUN_PLAY_ONCE | FAUN_PLAY_FADE_IN;

				int progLen = sizeof(crossfade);
				if (fadeOut == nullptr || fadeOut->eid == 0) {
					prog += 3;
					progLen -= 3;
				}
				faun_program(0, prog, progLen);
			}
		}
#endif
	}

	bool Event::Stop()
	{
		if (eid) {
			SoundEvent *s = GetEvent(eid);
			if (s) {
				faun_control(FAUN_PID_SOURCE(eid), 1, FC_STOP);
				DestroyEvent(s);
			}
			return s != nullptr;
		}
		return false;
	}

	bool Event::IsPlaying() const
	{
		if (eid && wavstream[FAUN_PID_SOURCE(eid)].sample)
			return faun_isPlaying(eid);
		return false;
	}

	bool Event::SetOp(Op /*op*/)
	{
		/* Currently unused by game code. */
		return false;
	}

	bool Event::VolumeAnimate(const float targetVolL, const float targetVolR, const float dv_dt1, const float dv_dt2)
	{
		SoundEvent *ev = GetEvent(eid);
#if 0
		if (dv_dt1 == 5.0f)
			printf("KR VolumeAnim %x vol;%f,%f dt:%f,%f %s\n",
				eid, targetVolL, targetVolR, dv_dt1, dv_dt2,
				ev ? ev->sample->path.c_str() : "-");
#endif
		if (ev) {
			faun_pan(FAUN_PID_SOURCE(eid), targetVolL, targetVolR, 1.0f/dv_dt1);
		}
		return (ev != nullptr);
	}

	bool Event::SetVolume(const float vol_left, const float vol_right)
	{
		//printf("KR Volume %x %f,%f\n", eid, vol_left, vol_right);
		SoundEvent *ev = GetEvent(eid);
		if (ev) {
			faun_pan(FAUN_PID_SOURCE(eid), vol_left, vol_right, 0.0f);
			return true;
		}
		return false;
	}

	bool Event::FadeOut(float dv_dt, Op op)
	{
		SoundEvent *ev = GetEvent(eid);
		if (ev) {
			// if (op & Sound::OP_REPEAT)
			int si = FAUN_PID_SOURCE(eid);
			faun_setParameter(si, 1, FAUN_FADE_PERIOD, 1.0f/dv_dt);
			faun_control(si, 1, FC_FADE_OUT);
		}
		return (ev != nullptr);
	}

	const std::vector<std::string> GetMusicFiles()
	{
		std::vector<std::string> songs;
		songs.reserve(sfx_samples.size());
		for (std::map<std::string, Sample>::const_iterator it = sfx_samples.begin();
			 it != sfx_samples.end(); ++it) {
			if (it->second.isMusic)
				songs.emplace_back(it->first.c_str());
		}
		return songs;
	}

} /* namespace Sound */
