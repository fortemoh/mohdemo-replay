// MoHAA Reloaded: the team icons above team mates' heads with the Competitive
// HUD (cg_hudstyle 1), drawn from its team outlines (ui/outlines/team/*.svg)
// at 128 px by the client at its start (cl_uicomp.cpp, UI_CompHud_HeadIcons).
// The game's shaders' settings (scripts/hud.shader), at a quarter of the
// sprite's scale: the game's pictures are 32 px.

textures/hud/reloaded_allies_headicon
{
	spritegen parallel
	spritescale 0.25
	surfaceparm nolightmap
	nopicmip
	cull none
	{
		alphaFunc GT0
		blendFunc blend
		rgbGen vertex
		alphagen vertex
		map textures/hud/reloaded_allies.tga
	}
}

textures/hud/reloaded_axis_headicon
{
	spritegen parallel
	spritescale 0.25
	surfaceparm nolightmap
	nopicmip
	cull none
	{
		alphaFunc GT0
		blendFunc blend
		rgbGen vertex
		alphagen vertex
		map textures/hud/reloaded_axis.tga
	}
}
