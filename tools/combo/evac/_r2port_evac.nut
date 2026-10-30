// Evacuation epilogue for the converted Titanfall 2 maps (r2port). One generic level script, included by a one-line
// mp/<map>.nut stub for every R2 map that has no level script of its own (tools/combo/stage-combo.ps1, "Evac" step).
//
// The TF1 evac system (mp/_evac.nut EvacMain) needs level.evacNode (a node the dropship lands at) and level.evacSpaceNode
// (where it warps to when it leaves). TF1 level scripts register them from named info_target entities. The R2 map entity
// files carry the same names on 13 of the 20 maps: escape_node1..3 (landing), spec_cam1..3 (spectator camera for the loser
// that watches the evac), spacenode / intro_SpaceNode (the space flyby origin). This script wires them up.
//
// A map without escape nodes or without a space node (mp_colony02 and the mp_lf_* maps) has no usable landing point: evac is
// switched off for the match (level.evacEnabled = false) so ShouldRunEvac() is false and the epilogue is the plain 6 s
// one instead of a 70 s dead timer with no dropship.

const R2PORT_EVAC_MAX_NODES = 8

function main()
{
	if ( reloadingScripts )
		return

	GM_AddEndRoundFunc( EndRoundMain )
}

function EntitiesDidLoad()
{
	if ( EvacEnabled() )
		EvacSetup()
}

function EvacSetup()
{
	local added = 0
	for ( local i = 1; i <= R2PORT_EVAC_MAX_NODES; i++ )
	{
		local nodeName = "escape_node" + i
		local node = GetEnt( nodeName )
		if ( !node )
			continue

		local specPos = node.GetOrigin() + Vector( 0, 0, 160 )
		local specAng = node.GetAngles()
		local spectatorNode = GetEnt( "spec_cam" + i )
		if ( spectatorNode )
		{
			specPos = spectatorNode.GetOrigin()
			specAng = spectatorNode.GetAngles()
		}

		Evac_AddLocation( nodeName, specPos, specAng )
		added++
	}

	local spaceNode = GetEnt( "intro_SpaceNode" )
	if ( !spaceNode )
		spaceNode = GetEnt( "spacenode" )

	if ( !added || !spaceNode )
	{
		level.evacEnabled = false
		printt( "R2 evac: " + GetMapName() + " has no usable evac setup (escape nodes " + added + ", space node " + ( spaceNode ? "yes" : "no" ) + "), evac disabled" )
		return
	}

	Evac_SetSpaceNode( spaceNode )
	Evac_SetupDefaultVDUs()
	printt( "R2 evac: " + GetMapName() + " evac locations " + added + ", space node at " + spaceNode.GetOrigin() )
}

function EndRoundMain()
{
	if ( EvacEnabled() && level.evacEnabled && level.ExtractLocations.len() )
		GM_SetObserverFunc( EvacObserverFunc )
}

function EvacObserverFunc( player )
{
	player.SetObserverModeStaticPosition( level.ExtractLocations[ level.SelectedExtractLocationIndex ].spectatorPos )
	player.SetObserverModeStaticAngles( level.ExtractLocations[ level.SelectedExtractLocationIndex ].spectatorAng )

	player.StartObserverMode( OBS_MODE_CHASE )
	player.SetObserverTarget( null )
}

main()
