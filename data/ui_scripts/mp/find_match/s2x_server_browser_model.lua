-- Browser presentation only. Discovery, validation and persistence stay in C++.
local model = {}

model.sortLabels = { "PLAYERS", "PING", "NAME", "MAP" }
model.hqSortLabels = { "PLAYERS", "PING", "NAME" }
model.filterLabels = { "ALL SERVERS", "HAS PLAYERS", "OPEN SLOTS" }

function model.plainText( text )
	return (text or ""):gsub( "%^%d", "" ):gsub( "[%c]", " " )
end

function model.visibleServers( servers, favoritesOnly, filter, sort, headquarters )
	local visible = {}

	for _, server in ipairs( servers ) do
		local matches = server.online or favoritesOnly
		matches = matches and (not favoritesOnly or server.favorite)
		-- An offline favorite's category is unknown until the server replies.
		matches = matches and (not server.online or ((server.headquarters == true) == (headquarters == true)))

		if filter == 2 then
			matches = matches and server.online and server.clients > server.bots
		elseif filter == 3 then
			matches = matches and server.online and server.clients - server.bots < server.maxClients
		end

		if matches then
			table.insert( visible, server )
		end
	end

	table.sort( visible, function ( a, b )
		if a.online ~= b.online then
			return a.online
		end

		if sort == 1 then
			local humansA, humansB = a.clients - a.bots, b.clients - b.bots

			if humansA ~= humansB then
				return humansA > humansB
			end
		elseif sort == 3 or sort == 4 then
			local field = sort == 3 and "hostname" or "map"
			local textA, textB = model.plainText( a[field] ):lower(), model.plainText( b[field] ):lower()

			if textA ~= textB then
				return textA < textB
			end
		end

		if a.ping ~= b.ping then
			return a.ping < b.ping
		end

		return a.address < b.address
	end )

	return visible
end

function model.sameServers( previous, current )
	if #previous ~= #current then
		return false
	end

	for index, server in ipairs( previous ) do
		for _, field in ipairs( {
			"address", "hostname", "map", "mapReference", "gametype", "clients",
			"bots", "maxClients", "ping", "favorite", "online", "status", "headquarters"
		} ) do
			if server[field] ~= current[index][field] then
				return false
			end
		end
	end

	return true
end

function model.selectionIndex( servers, address )
	for index, server in ipairs( servers ) do
		if server.address == address then
			return index
		end
	end

	return #servers > 0 and 1 or nil
end

return model
