main()
{
    if ( getdvar( "mapname" ) == "mp_sandbox_01" )
    {
        return;
    }

    replacefunc( maps\mp\gametypes\_class::_id_1F97, ::getCostumeByIndex );
}

getCostumeByIndex( costumeIndex )
{
    // Bots have no profile costume data.
    if ( isbot( self ) )
    {
        if ( isdefined( self._id_267E ) && self._id_267E.size > 0 )
        {
            return self._id_267E;
        }

        division = 0;

        if ( isdefined( self._id_0079 ) && self._id_0079 != 5 )
        {
            division = self._id_0079;
        }

        // Use Allies with the stock division fallback.
        return _func_333( division, 1 );
    }

    costume = [];

    for ( categoryIndex = 0; categoryIndex < level._id_2683.size; categoryIndex++ )
    {
        category = level._id_2683[categoryIndex];

        if ( maps\mp\gametypes\_class::_id_1F9B( category ) )
        {
            costume[categoryIndex] = maps\mp\gametypes\_class::_id_1F98( category );
            continue;
        }

        costume[categoryIndex] = maps\mp\gametypes\_class::_id_1F9A( category, costumeIndex );
    }

    faceCamoId = maps\mp\gametypes\_class::_id_1F9A( "faceCamoId", costumeIndex );

    if ( isdefined( faceCamoId ) && faceCamoId >= 0 )
    {
        costume[level._id_2683.size] = faceCamoId;
    }

    return costume;
}
