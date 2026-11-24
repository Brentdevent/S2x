if game:issingleplayer() or not Engine.InFrontend() then
	return
end

-- Download menus, driven by download.cpp through s2x_download_* events
s2x_download_ui = true

local PROGRESS_MENU = "s2x_download_progress"

local function get_controller( event )
	return event and event.controller or Engine.GetFirstActiveController()
end

local function format_size( bytes )
	if bytes >= 1073741824 then
		return string.format( "%.2f GB", bytes / 1073741824 )
	end

	return string.format( "%.1f MB", bytes / 1048576 )
end

local function position( element, left, right, top, bottom )
	element:setAnchorsAndPosition( 0, 1, 0, 1, left * _1080p, right * _1080p, top * _1080p, bottom * _1080p, 0 )
end

local function add_image( parent, left, right, top, bottom, color, alpha )
	local image = LUI.UIImage.new()
	position( image, left, right, top, bottom )
	image:setRGBFromInt( color, 0 )
	image:setAlpha( alpha, 0 )
	parent:addElement( image )
	return image
end

local function add_text( parent, properties, left, right, top, bottom, size, gold, bold )
	local text = LUI.UIText.new()
	position( text, left, right, top, bottom )
	text:setFont( bold and FONTS.BodyBoldFont.Font or FONTS.BodyFont.Font )
	text:setFontSize( size, 0 )
	text:setHorizontalAlignment( LUI.HorizontalAlignment.Left )
	text:setVerticalAlignment( LUI.VerticalAlignment.Middle )
	text:setRGBFromInt( gold and SWATCHES.Menus.MenuGold or SWATCHES.Menus.MenuOffWhite, 0 )
	if properties.fontIconSet then
		text:setFontIconSet( properties.fontIconSet )
	end
	parent:addElement( text )
	return text
end

local BAR_LEFT, BAR_RIGHT = 500, 1420

LUI.MenuBuilder.registerPopupType( PROGRESS_MENU, function ( menu, options )
	options = options or {}
	local controller = options.controllerIndex or Engine.GetFirstActiveController()
	local properties = { controllerIndex = controller, fontIconSet = options.fontIconSet }

	local self = LUI.UIElement.new( { left = 0, right = 0, top = 0, bottom = 0,
		leftAnchor = true, rightAnchor = true, topAnchor = true, bottomAnchor = true } )
	self.id = PROGRESS_MENU

	add_image( self, 0, 1920, 0, 1080, 0x000000, 0.7 )
	add_image( self, 460, 1460, 380, 700, 0x292B2B, 0.95 )

	local title = add_text( self, properties, BAR_LEFT, BAR_RIGHT, 405, 455, 34, true, true )
	title:setText( "DOWNLOADING CUSTOM MAP", 0 )

	local map_text = add_text( self, properties, BAR_LEFT, BAR_RIGHT, 470, 505, 24 )
	local file_text = add_text( self, properties, BAR_LEFT, BAR_RIGHT, 515, 550, 20 )

	add_image( self, BAR_LEFT, BAR_RIGHT, 575, 600, 0x111111, 1 )
	local bar = add_image( self, BAR_LEFT, BAR_LEFT, 575, 600, SWATCHES.Menus.MenuGold, 1 )

	local amount_text = add_text( self, properties, BAR_LEFT, BAR_RIGHT, 615, 650, 22 )

	local helpers = LUI.MenuBuilder.BuildRegisteredType( "button_helper_bar", properties )
	position( helpers, 0, 1920, 975, 1025 )
	self:addElement( helpers )
	helpers:BeginSet():AddBackButton():Finish()

	local closed = false
	local function close()
		if not closed then
			closed = true
			ACTIONS.LeaveMenu( self )
		end
	end

	-- Closes once the transfer stops
	self:registerEventHandler( "button_secondary", function ()
		s2x_download.abort()
		file_text:setText( "Cancelling...", 0 )
	end )

	local function update()
		local progress = s2x_download.getprogress()
		if not progress.active then
			close()
			return
		end

		map_text:setText( string.format( "'%s' from %s", progress.map, progress.host ), 0 )
		if progress.file ~= "" then
			file_text:setText( string.format( "%s: %s / %s", progress.file,
				format_size( progress.filedone ), format_size( progress.filetotal ) ), 0 )
		end

		local fraction = progress.total > 0 and math.min( progress.done / progress.total, 1 ) or 0
		position( bar, BAR_LEFT, BAR_LEFT + ( BAR_RIGHT - BAR_LEFT ) * fraction, 575, 600 )
		amount_text:setText( string.format( "%s / %s (%d%%)", format_size( progress.done ),
			format_size( progress.total ), math.floor( fraction * 100 ) ), 0 )
	end

	local timer = LUI.UITimer.new( 250, { name = "s2x_download_tick", dispatchChildren = false },
		nil, false, false, false, false )
	timer.timerEventTarget = timer
	timer:addEventHandler( "s2x_download_tick", update )
	self:addElement( timer )

	update()
	return self
end )

local root = Engine.GetLuiRoot()

root:registerEventHandler( "s2x_download_prompt", function ( element, event )
	local id = event.id
	local answered = false
	local function respond( accepted )
		if not answered then
			answered = true
			s2x_download.respond( id, accepted )
		end
	end

	LUI.FlowManager.RequestAddMenu( element, "generic_yesno_popup", false, get_controller( event ), false, {
		popup_title = "DOWNLOAD CUSTOM MAP",
		message_text = event.message,
		cancel_means_no = true,
		yes_action = function ()
			respond( true )
		end,
		no_action = function ()
			respond( false )
		end
	} )
end )

root:registerEventHandler( "s2x_download_start", function ( element, event )
	if LUI.FlowManager.IsMenuInStack( root, PROGRESS_MENU ) then
		return
	end

	LUI.FlowManager.RequestAddMenu( element, PROGRESS_MENU, false, get_controller( event ), false, {} )
end )

root:registerEventHandler( "s2x_download_error", function ( element, event )
	LUI.FlowManager.RequestAddMenu( element, "generic_confirmation_popup", false, get_controller( event ), false, {
		popup_title = Engine.Localize( "MENU_ERROR" ),
		message_text = event.message
	} )
end )
