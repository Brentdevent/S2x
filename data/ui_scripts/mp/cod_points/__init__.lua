if game:issingleplayer() or not Engine.InFrontend() then
	return
end

-- Reuse the shipped store tiles and menu chrome with native bundle records.
-- No itemData is attached: StoreItemButton's platform-commerce action stays inert.
LUI.MenuBuilder.registerPopupType( "s2x_cod_points", function ( menu, options )
	options = options or {}
	local controller = options.controllerIndex or Engine.GetFirstActiveController()
	local self = LUI.UIElement.new( { left = 0, right = 0, top = 0, bottom = 0,
		leftAnchor = true, rightAnchor = true, topAnchor = true, bottomAnchor = true } )
	self.id = "s2x_cod_points"
	local properties = { controllerIndex = controller, fontIconSet = options.fontIconSet }
	local function place( element, left, top, right, bottom )
		element:setAnchors( 0, 1, 0, 1, 0 )
		element:setLeft( left * _1080p, 0 )
		element:setRight( right * _1080p, 0 )
		element:setTop( top * _1080p, 0 )
		element:setBottom( bottom * _1080p, 0 )
		self:addElement( element )
	end
	local background = LUI.MenuBuilder.BuildRegisteredType( "GenericMenuBackground", properties )
	background:setAnchors( 0, 0, 0, 0, 0 )
	background:setLeft( 0, 0 )
	background:setRight( 0, 0 )
	background:setTop( 0, 0 )
	background:setBottom( 0, 0 )
	background.Background:setAlpha( 1, 0 )
	self:addElement( background )
	local title = LUI.MenuBuilder.BuildRegisteredType( "GenericMenuTitle", properties )
	place( title, 100, 125, 1820, 173 )
	title.Title:setText( "CALL OF DUTY® POINTS", 0 )
	local helpers = LUI.MenuBuilder.BuildRegisteredType( "button_helper_bar", properties )
	place( helpers, 0, 975, 1920, 1025 )
	helpers:BeginSet():AddBackButton():AddLeft( LuaButton.primary, "LUA_MENU_SELECT", nil ):Finish()

	local committed, finished = false, false
	local denominations = {}
	local tiles = {}
	for _, bundle in ipairs( Engine.Economy_GetCodPointBundles() ) do
		if not denominations[bundle.amount] then
			denominations[bundle.amount] = true
			local transaction = Engine.Economy_CreateTransactionId()
			local index = #tiles
			local tile = LUI.MenuBuilder.BuildRegisteredType( "StoreItemButton", properties )
			tile.id = "cp_" .. bundle.id
			local x, y = 170 + (index % 3) * 535, 240 + math.floor( index / 3 ) * 305
			place( tile, x, y, x + 510, y + 285 )
			tile.Button.BGImage:setImage( RegisterMaterial( bundle.image ), 0 )
			tile.Button.TitleLabel:setText( Engine.Localize( bundle.title ), 0 )
			tile.Button.TitleLabel:setAlpha( 1, 0 )
			tile.Tag:setAlpha( 0, 0 )
			tile.TagBkg:setAlpha( 0, 0 )
			tile.Cost:setAlpha( 0, 0 )
			tile.CostBkg:setAlpha( 0, 0 )
			tile:addEventHandler( "button_action", function ( element, event )
				if committed or finished then
					return
				end
				LUI.FlowManager.RequestAddMenu( element, "notification_modal", true, controller, false, {
					titleText = Engine.Localize( bundle.title ),
					descText = "Add " .. tostring( bundle.amount ) .. " COD Points to your balance?",
					icon = bundle.image,
					modalType = ModalUtils.NotificationModalType.GeneralNotifications,
					accept_func = function ()
						if committed or finished then
							return
						end
						if Engine.Economy_TopUpCodPoints( bundle.id, transaction ) then
							committed = true
						else
							-- Failed saves retain this attempt ID; reopening after success
							-- creates a new transaction for an intentional repeat purchase.
							ACTIONS.PlayErrorSound( self )
							LUI.FlowManager.RequestAddMenu( self, "generic_confirmation_popup", false,
								controller, false, { popup_title = Engine.Localize( "MENU_ERROR" ),
									message_text = "The purchase could not be completed. Please try again." } )
						end
					end,
					cancel_func = function () end,
					choices = {}
				} )
			end )
			tiles[#tiles + 1] = tile
		end
	end
	for i, tile in ipairs( tiles ) do
		tile:initNavTables()
		tile.navigation = {
			left = i % 3 ~= 1 and tiles[i - 1] or nil,
			right = i % 3 ~= 0 and tiles[i + 1] or nil,
			up = tiles[i - 3],
			down = tiles[i + 3]
		}
	end
	self:registerEventHandler( "update_currency", function ()
		-- Sent only after the native wallet fetch applies absolute balances.
		if committed and not finished then
			finished = true
			self:dispatchEventToRoot( { name = "purchase_complete", controller = controller } )
			ACTIONS.LeaveMenu( self )
		end
	end )
	return self
end )

local stock_add_menu = LUI.FlowManager.RequestAddMenu
LUI.FlowManager.RequestAddMenu = function ( element, name, ... )
	local args = { ... }
	local options = args[4]
	if name == "store_menu" and options and options.linkedItem then
		-- Keep the native CP entry offline even if its catalog is temporarily
		-- unavailable. Never fall through to platform checkout on that path.
		if string.match( options.linkedItem, "^%d+codpointsB?$" ) then
			local bundles = Engine.Economy_GetCodPointBundles()
			if #bundles == 0 then
				return stock_add_menu( element, "generic_confirmation_popup", false, args[2], false, {
					popup_title = Engine.Localize( "MENU_ERROR" ),
					message_text = "COD Point bundles are not available yet. Please reopen this menu." } )
			end
			return stock_add_menu( element, "s2x_cod_points", false, args[2], false, {} )
		end
	end
	return stock_add_menu( element, name, ... )
end
