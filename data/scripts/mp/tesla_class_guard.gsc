main()
{
    replacefunc( maps\mp\gametypes\_class::_id_09A7, ::prepareLoadout );
}

isTeslaWeaponName( weaponName )
{
    if ( !isdefined( weaponName ) || weaponName == "" || weaponName == "none" )
    {
        return 0;
    }

    // Normalize attachments, mode suffixes, and variants.
    weaponName = maps\mp\_utility::_id_4431( weaponName );

    if ( issubstr( weaponName, "_raid" ) )
    {
        weaponName = _func_337( weaponName, "_raid" );
    }

    if ( issubstr( weaponName, "_zm" ) )
    {
        weaponName = _func_337( weaponName, "_zm" ) + "_mp";
    }

    weaponName = maps\mp\gametypes\_class::_id_4432( weaponName );

    return weaponName == "teslagunmtx_mp"
        || weaponName == "teslagun_war_regular_mp"
        || weaponName == "teslagun_war_moon_mp";
}

isTeslaWeapon( weapon )
{
    if ( !isdefined( weapon ) || !isdefined( weapon.guid ) )
    {
        return 0;
    }

    // Resolve the GUID without mode suffixes.
    return isTeslaWeaponName( maps\mp\_utility::_id_452B( weapon.guid ) );
}

getFallbackWeapon( slot )
{
    // Use the stock invalid-weapon fallback.
    guid = maps\mp\gametypes\_class::table_getweapon( level.classtablename, 10, slot );
    weapon = maps\mp\_utility::_id_473C( guid, 0 );

    if ( isTeslaWeapon( weapon ) )
    {
        weapon = maps\mp\_utility::_id_473C( 0, 0 );
    }

    return weapon;
}

buildFallbackWeapon( weapon, division )
{
    return maps\mp\gametypes\_class::_id_1D66(
        maps\mp\_utility::_id_4737( weapon ),
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, self, division );
}

sanitizeLoadout( loadout )
{
    if ( !isdefined( loadout ) )
    {
        return;
    }

    // Cached weapon names may be stale; check GUIDs too.
    if ( isTeslaWeapon( loadout._id_7709 ) || isTeslaWeaponName( loadout._id_76F8 ) )
    {
        loadout._id_7709 = getFallbackWeapon( 0 );
        loadout._id_76F3 = [];

        for ( attachmentIndex = 0; attachmentIndex < 6; attachmentIndex++ )
        {
            loadout._id_76F3[attachmentIndex] = 0;
        }

        loadout._id_76F5 = 0;
        loadout._id_76F4 = 0;
        loadout._id_7700 = 0;
        loadout._id_0144 = 0;
        loadout.primarypaintjobid = 0;
        loadout.primarycharmguid = 0;
        loadout._id_76F8 = buildFallbackWeapon( loadout._id_7709, loadout._id_0079 );
        maps\mp\gametypes\_class::_id_7B3D();
    }

    if ( isTeslaWeapon( loadout._id_835D ) || isTeslaWeaponName( loadout._id_8358 ) )
    {
        loadout._id_835D = getFallbackWeapon( 1 );
        loadout._id_8353 = [];

        for ( attachmentIndex = 0; attachmentIndex < 6; attachmentIndex++ )
        {
            loadout._id_8353[attachmentIndex] = 0;
        }

        loadout._id_8355 = 0;
        loadout._id_8354 = 0;
        loadout._id_8359 = 0;
        loadout._id_0176 = 0;
        loadout.secondarypaintjobid = 0;
        loadout.secondarycharmguid = 0;
        loadout._id_8358 = buildFallbackWeapon( loadout._id_835D, loadout._id_0079 );
        maps\mp\gametypes\_class::_id_7B3D();
    }

    // Melee bypasses primary/secondary validation.
    if ( isTeslaWeapon( loadout._id_60FC ) )
    {
        loadout._id_60FC = undefined;
        maps\mp\gametypes\_class::_id_7B3D();
    }

    // Copied and recipe loadouts can bypass equipment validation.
    if ( isTeslaWeapon( loadout._id_37FE ) )
    {
        loadout._id_37FE = maps\mp\_utility::_id_44CE( 0, 0 );
        loadout._id_37FD = 0;
        maps\mp\gametypes\_class::_id_7B3D();
    }

    if ( isTeslaWeapon( loadout._id_69AD ) )
    {
        loadout._id_69AD = maps\mp\_utility::_id_44CE( 0, 0 );
        loadout._id_69AC = 0;
        maps\mp\gametypes\_class::_id_7B3D();
    }
}

prepareLoadout( loadout )
{
    // Sanitize before the stock early return.
    if ( isplayer( self ) && !maps\mp\_utility::_id_585F() )
    {
        sanitizeLoadout( loadout );
    }

    if ( !getdvarint( "1936" ) || maps\mp\_utility::_id_551F() )
    {
        return;
    }

    if ( !isagent( self ) )
    {
        if ( isdefined( level._id_446F ) )
        {
            loadout._id_7ED2 = self [[ level._id_446F ]]();
        }
        else
        {
            loadout._id_7ECF = maps\mp\gametypes\_class::_id_7ECD();
            loadout.slot = maps\mp\gametypes\_class::_id_7ECE();
            loadout._id_7ED2 = maps\mp\gametypes\_class::_id_4667( loadout._id_7ECF, loadout.slot );
        }
    }
}
