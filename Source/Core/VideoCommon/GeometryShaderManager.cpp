// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <cfloat>
#include <cmath>

#include "VideoCommon/BPMemory.h"
#include "VideoCommon/GeometryShaderGen.h"
#include "VideoCommon/GeometryShaderManager.h"
#include "VideoCommon/VideoCommon.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/XFMemory.h"

static const int LINE_PT_TEX_OFFSETS[8] = {
	0, 16, 8, 4, 2, 1, 1, 1
};

GeometryShaderConstants GeometryShaderManager::constants;
bool GeometryShaderManager::dirty;

static bool s_projection_changed;
static bool s_viewport_changed;

void GeometryShaderManager::Init()
{
	memset(&constants, 0, sizeof(constants));

	Dirty();
}

void GeometryShaderManager::Shutdown()
{
}

void GeometryShaderManager::Dirty()
{
	SetViewportChanged();
	SetProjectionChanged();
	SetLinePtWidthChanged();

	for (int i = 0; i < 8; i++)
		SetTexCoordChanged(i);

	dirty = true;
}

void GeometryShaderManager::SetConstants()
{
	if (s_projection_changed && g_ActiveConfig.iStereoMode > 0)
	{
		s_projection_changed = false;

		if (xfmem.projection.type == GX_PERSPECTIVE)
		{
			float offset = (g_ActiveConfig.iStereoDepth / 1000.0f) * (g_ActiveConfig.iStereoDepthPercentage / 100.0f);
			constants.stereoparams[0] = g_ActiveConfig.bStereoSwapEyes ? offset : -offset;
			constants.stereoparams[1] = g_ActiveConfig.bStereoSwapEyes ? -offset : offset;
			constants.stereoparams[2] = g_ActiveConfig.iStereoConvergence * (g_ActiveConfig.iStereoConvergencePercentage / 100.0f);
		}
		else
		{
			constants.stereoparams[0] = constants.stereoparams[1] = 0;
		}

		dirty = true;
	}

	if (s_viewport_changed)
	{
		s_viewport_changed = false;

		constants.lineptparams[0] = 2.0f * xfmem.viewport.wd;
		constants.lineptparams[1] = -2.0f * xfmem.viewport.ht;

		dirty = true;
	}
}

void GeometryShaderManager::SetViewportChanged()
{
	s_viewport_changed = true;
}

void GeometryShaderManager::SetProjectionChanged()
{
	s_projection_changed = true;
}

void GeometryShaderManager::SetLinePtWidthChanged()
{
	constants.lineptparams[2] = bpmem.lineptwidth.linesize / 6.f;
	constants.lineptparams[3] = bpmem.lineptwidth.pointsize / 6.f;
	constants.texoffset[2] = LINE_PT_TEX_OFFSETS[bpmem.lineptwidth.lineoff];
	constants.texoffset[3] = LINE_PT_TEX_OFFSETS[bpmem.lineptwidth.pointoff];
	dirty = true;
}

void GeometryShaderManager::SetTexCoordChanged(u8 texmapid)
{
	TCoordInfo& tc = bpmem.texcoords[texmapid];
	int bitmask = 1 << texmapid;
	constants.texoffset[0] &= ~bitmask;
	constants.texoffset[0] |= tc.s.line_offset << texmapid;
	constants.texoffset[1] &= ~bitmask;
	constants.texoffset[1] |= tc.s.point_offset << texmapid;
	dirty = true;
}

void GeometryShaderManager::DoState(PointerWrap &p)
{
	if (p.GetMode() == PointerWrap::MODE_READ)
	{
		// Reload current state from global GPU state
		// NOTE: This requires that all GPU memory has been loaded already.
		Dirty();
	}
}
