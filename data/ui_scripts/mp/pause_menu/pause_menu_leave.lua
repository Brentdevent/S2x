local f0_local0 = require( "pause_menu_leave_uc" )
local f0_local1, f0_local2, f0_local3, f0_local4, f0_local5, f0_local6 = nil

if f0_local0 ~= nil and type( f0_local0 ) == "table" then
	f0_local1 = f0_local0.PreLoadFunc
	f0_local2 = f0_local0.PostLoadFunc
	f0_local3 = f0_local0.PushFunc
	f0_local4 = f0_local0.PushOverFunc
	f0_local5 = f0_local0.ResumeFunc
	f0_local6 = f0_local0.PopFunc
end

local menu_builders = LUI.MenuBuilder.m_types_build or m_types_build
menu_builders["pause_menu_leave"] = function ( menu, controller )
	local self = LUI.UIVerticalNavigator.new( {
		left = 0 * _1080p,
		right = 0 * _1080p,
		top = 0 * _1080p,
		bottom = 0 * _1080p,
		leftAnchor = true,
		rightAnchor = true,
		topAnchor = true,
		bottomAnchor = true
	} )

	self.id = "pause_menu_leave"
	local f1_local1 = controller or {}
	local f1_local2 = f1_local1.controllerIndex
	if not f1_local2 then
		if Engine.InFrontend() then
			local f1_local3 = LUI.FlowManager.GetScopedData( self )
			assert( f1_local3 )
			f1_local2 = f1_local3.exclusiveControllerIndex
		else
			f1_local2 = self:getRootController()
		end
	end

	if f0_local1 then
		f0_local1( self, f1_local2, f1_local1 )
	end

	self:playSound( "menu_open" )
	local f1_local3 = self
	
	local ReturnToMatchButton = nil
	ReturnToMatchButton = LUI.MenuBuilder.BuildRegisteredType( "GenericButton", {
		controllerIndex = f1_local2,
		fontIconSet = f1_local1.fontIconSet
	} )

	ReturnToMatchButton.id = "ReturnToMatchButton"
	self:addElement( ReturnToMatchButton )
	self.ReturnToMatchButton = ReturnToMatchButton
	
	ReturnToMatchButton:setAnchors( 0, 1, 0, 1, 0 )
	ReturnToMatchButton:setBottom( _1080p * 530, 0 )
	ReturnToMatchButton:setLeft( _1080p * 100, 0 )
	ReturnToMatchButton:setRight( _1080p * 600, 0 )
	ReturnToMatchButton:setTop( _1080p * 468, 0 )

	if ReturnToMatchButton.ButtonBackground then
		ReturnToMatchButton.ButtonBackground:setLeft( _1080p * -100, 0 )
		ReturnToMatchButton.ButtonBackground:setRight( _1080p * 0, 0 )
	end

	if ReturnToMatchButton.Name then
		ReturnToMatchButton.Name:setFontSize( 22, 0 )
		ReturnToMatchButton.Name:setHorizontalAlignment( LUI.HorizontalAlignment.Left )
		ReturnToMatchButton.Name:setText( Engine.Localize( "LUA_MENU_RETURN_BUTTON" ), 0 )
	end

	local LeaveMatchButton = nil
	
	LeaveMatchButton = LUI.MenuBuilder.BuildRegisteredType( "GenericButton", {
		controllerIndex = f1_local2,
		fontIconSet = f1_local1.fontIconSet
	} )
	LeaveMatchButton.id = "LeaveMatchButton"
	self:addElement( LeaveMatchButton )
	self.LeaveMatchButton = LeaveMatchButton
	
	LeaveMatchButton:setAnchors( 0, 1, 0, 1, 0 )
	LeaveMatchButton:setBottom( _1080p * 595, 0 )
	LeaveMatchButton:setLeft( _1080p * 100, 0 )
	LeaveMatchButton:setRight( _1080p * 600, 0 )
	LeaveMatchButton:setTop( _1080p * 533, 0 )

	if LeaveMatchButton.ButtonBackground then
		LeaveMatchButton.ButtonBackground:setLeft( _1080p * -100, 0 )
		LeaveMatchButton.ButtonBackground:setRight( _1080p * 0, 0 )
	end

	if LeaveMatchButton.Name then
		LeaveMatchButton.Name:setFontSize( 22, 0 )
		LeaveMatchButton.Name:setHorizontalAlignment( LUI.HorizontalAlignment.Left )
		LeaveMatchButton.Name:setText( Engine.Localize( "LUA_MENU_LEAVE_BUTTON" ), 0 )
	end

	local EndMatchButton = nil
	if f0_local0.CanEndPrivateMatch() then
		EndMatchButton = LUI.MenuBuilder.BuildRegisteredType( "GenericButton", {
			controllerIndex = f1_local2,
			fontIconSet = f1_local1.fontIconSet,
			buttonText = Engine.ToUpperCase( Engine.Localize( "End Match" ) ),
			desc_text = Engine.Localize( "End the match for all players." )
		} )
		EndMatchButton.id = "EndMatchButton"
		EndMatchButton:makeFocusable()
		self:addElement( EndMatchButton )
		self.EndMatchButton = EndMatchButton
		EndMatchButton:setAnchors( 0, 1, 0, 1, 0 )
		EndMatchButton:setLeft( _1080p * 100, 0 )
		EndMatchButton:setRight( _1080p * 600, 0 )
		EndMatchButton:setTop( _1080p * 598, 0 )
		EndMatchButton:setBottom( _1080p * 660, 0 )
		EndMatchButton.ButtonBackground:setLeft( _1080p * -100, 0 )
		EndMatchButton.ButtonBackground:setRight( 0, 0 )
		EndMatchButton.Name:setFontSize( 22, 0 )
		EndMatchButton.Name:setHorizontalAlignment( LUI.HorizontalAlignment.Left )
	end

	local ShutdownButton = nil
	if CONDITIONS.IsPC( self ) then
		ShutdownButton = LUI.MenuBuilder.BuildRegisteredType( "GenericButton", {
			controllerIndex = f1_local2,
			fontIconSet = f1_local1.fontIconSet
		} )
		ShutdownButton.id = "ShutdownButton"
		self:addElement( ShutdownButton )
		self.ShutdownButton = ShutdownButton
		
		ShutdownButton:setAnchors( 0, 1, 0, 1, 0 )
		ShutdownButton:setBottom( _1080p * (EndMatchButton and 725 or 660), 0 )
		ShutdownButton:setLeft( _1080p * 100, 0 )
		ShutdownButton:setRight( _1080p * 600, 0 )
		ShutdownButton:setTop( _1080p * (EndMatchButton and 663 or 598), 0 )
		if ShutdownButton.ButtonBackground then
			ShutdownButton.ButtonBackground:setLeft( _1080p * -100, 0 )
			ShutdownButton.ButtonBackground:setRight( _1080p * 0, 0 )
		end
		if ShutdownButton.Name then
			ShutdownButton.Name:setFontSize( 22, 0 )
			ShutdownButton.Name:setHorizontalAlignment( LUI.HorizontalAlignment.Left )
			ShutdownButton.Name:setText( Engine.Localize( "LUA_MENU_LEAVE_BUTTON" ), 0 )
		end

	end

	local button_helper_bar = nil
	button_helper_bar = LUI.MenuBuilder.BuildRegisteredType( "button_helper_bar", {
		controllerIndex = f1_local2,
		fontIconSet = f1_local1.fontIconSet
	} )
	button_helper_bar.id = "button_helper_bar"
	self:addElement( button_helper_bar )
	self.button_helper_bar = button_helper_bar
	button_helper_bar:setAnchors( 0, 0, 1, 0, 0 )
	button_helper_bar:setBottom( _1080p * -55, 0 )
	button_helper_bar:setLeft( _1080p * 0, 0 )
	button_helper_bar:setRight( _1080p * 0, 0 )
	button_helper_bar:setTop( _1080p * -105, 0 )
	
	local ConfirmationText = nil
	if not CONDITIONS.IsSingleplayer( self ) then
		ConfirmationText = LUI.UIText.new()
		ConfirmationText.id = "ConfirmationText"
		self:addElement( ConfirmationText )
		self.ConfirmationText = ConfirmationText
		
		if f1_local1.fontIconSet ~= nil then
			ConfirmationText:setFontIconSet( f1_local1.fontIconSet )
		end
		ConfirmationText:setAlpha( GLOBAL_CONSTANTS.MenuSubsectionHeaderOpacity, 0 )
		ConfirmationText:setAnchors( 0, 1, 0, 1, 0 )
		ConfirmationText:setBottom( _1080p * 390, 0 )
		ConfirmationText:setFont( FONTS.BodyBoldFont.Font )
		ConfirmationText:setFontSize( 40, 0 )
		ConfirmationText:setHorizontalAlignment( LUI.HorizontalAlignment.Left )
		ConfirmationText:setLeft( _1080p * 100, 0 )
		ConfirmationText:setRGBFromInt( SWATCHES.Menus.MenuWhite, 0 )
		ConfirmationText:setRight( _1080p * 1062.16, 0 )
		ConfirmationText:setText( Engine.Localize( "MENU_LEAVE_GAME_CONFIRMATION" ), 0 )
		ConfirmationText:setTop( _1080p * 344, 0 )
		ConfirmationText:setVerticalAlignment( LUI.VerticalAlignment.Middle )
	end

	local ConfirmationTextSP = nil
	if CONDITIONS.IsSingleplayer( self ) then
		ConfirmationTextSP = LUI.UIText.new()
		ConfirmationTextSP.id = "ConfirmationTextSP"
		self:addElement( ConfirmationTextSP )
		self.ConfirmationTextSP = ConfirmationTextSP
		
		if f1_local1.fontIconSet ~= nil then
			ConfirmationTextSP:setFontIconSet( f1_local1.fontIconSet )
		end
		ConfirmationTextSP:setAlpha( GLOBAL_CONSTANTS.MenuSubsectionHeaderOpacity, 0 )
		ConfirmationTextSP:setAnchors( 0, 1, 0, 1, 0 )
		ConfirmationTextSP:setBottom( _1080p * 389.4, 0 )
		ConfirmationTextSP:setFont( FONTS.BodyBoldFont.Font )
		ConfirmationTextSP:setFontSize( 40, 0 )
		ConfirmationTextSP:setHorizontalAlignment( LUI.HorizontalAlignment.Left )
		ConfirmationTextSP:setLeft( _1080p * 100, 0 )
		ConfirmationTextSP:setRGBFromInt( SWATCHES.Menus.MenuWhite, 0 )
		ConfirmationTextSP:setRight( _1080p * 1372.88, 0 )
		ConfirmationTextSP:setText( Engine.Localize( "MENU_ARE_YOU_SURE_QUIT" ), 0 )
		ConfirmationTextSP:setTop( _1080p * 344.4, 0 )
		ConfirmationTextSP:setVerticalAlignment( LUI.VerticalAlignment.Middle )
	end

	local WarningText = nil
	if not CONDITIONS.IsSingleplayer( self ) and not CONDITIONS.IsPrivateMatch( self ) then
		WarningText = LUI.UIText.new()
		WarningText.id = "WarningText"
		self:addElement( WarningText )
		self.WarningText = WarningText
		
		if f1_local1.fontIconSet ~= nil then
			WarningText:setFontIconSet( f1_local1.fontIconSet )
		end
		WarningText:setAlpha( GLOBAL_CONSTANTS.GenericMenuTextOpacity, 0 )
		WarningText:setAnchors( 0, 1, 0, 1, 0 )
		WarningText:setBottom( _1080p * 458.56, 0 )
		WarningText:setFont( FONTS.BodyFont.Font )
		WarningText:setFontSize( 28, 0 )
		WarningText:setHorizontalAlignment( LUI.HorizontalAlignment.Left )
		WarningText:setLeft( _1080p * 100, 0 )
		WarningText:setRGBFromInt( SWATCHES.Menus.MenuWhite, 0 )
		WarningText:setRight( _1080p * 1714.64, 0 )
		WarningText:setText( Engine.Localize( "LUA_MENU_LEAVE_GAME_DESC" ), 0 )
		WarningText:setTop( _1080p * 394.07, 0 )
		WarningText:setVerticalAlignment( LUI.VerticalAlignment.Top )
	end

	local WarningTextSP = nil
	if CONDITIONS.IsSingleplayer( self ) then
		WarningTextSP = LUI.UIText.new()
		WarningTextSP.id = "WarningTextSP"
		self:addElement( WarningTextSP )
		self.WarningTextSP = WarningTextSP
		
		if f1_local1.fontIconSet ~= nil then
			WarningTextSP:setFontIconSet( f1_local1.fontIconSet )
		end
		WarningTextSP:setAlpha( GLOBAL_CONSTANTS.GenericMenuTextOpacity, 0 )
		WarningTextSP:setAnchors( 0, 1, 0, 1, 0 )
		WarningTextSP:setBottom( _1080p * 434.4, 0 )
		WarningTextSP:setFont( FONTS.BodyFont.Font )
		WarningTextSP:setFontSize( 28, 0 )
		WarningTextSP:setHorizontalAlignment( LUI.HorizontalAlignment.Left )
		WarningTextSP:setLeft( _1080p * 100, 0 )
		WarningTextSP:setRGBFromInt( SWATCHES.Menus.MenuWhite, 0 )
		WarningTextSP:setRight( _1080p * 1820, 0 )
		WarningTextSP:setText( Engine.Localize( "MENU_ALL_CURRENT_PROGRESS_WILL_BE_LOST" ), 0 )
		WarningTextSP:setTop( _1080p * 389.4, 0 )
		WarningTextSP:setVerticalAlignment( LUI.VerticalAlignment.Middle )
	end
	
	if ConfirmationText then
		self.ConfirmationText:RegisterAnimationSequences( {
			Splitscreen = {
				{
					function ()
						return self.ConfirmationText:setAnchorsAndPosition( 0, 1, 0, 1, _1080p * 118, _1080p * 1080.16, _1080p * 202, _1080p * 248, 0 )
					end
				}
			}
		} )
	end
	if ConfirmationTextSP then
		self.ConfirmationTextSP:RegisterAnimationSequences( {
			Splitscreen = {
				{
					function ()
						return self.ConfirmationTextSP:setAnchorsAndPosition( 0, 1, 0, 1, _1080p * 118, _1080p * 1289.33, _1080p * 202.4, _1080p * 247.4, 0 )
					end
				}
			}
		} )
	end
	self.LeaveMatchButton:RegisterAnimationSequences( {
		Splitscreen = {
			{
				function ()
					return self.LeaveMatchButton:setAnchorsAndPosition( 0, 1, 0, 1, _1080p * 100, _1080p * 600, _1080p * 421, _1080p * 483, 0 )
				end
			}
		}
	} )
	self.ReturnToMatchButton:RegisterAnimationSequences( {
		Splitscreen = {
			{
				function ()
					return self.ReturnToMatchButton:setAnchorsAndPosition( 0, 1, 0, 1, _1080p * 100, _1080p * 600, _1080p * 356, _1080p * 418, 0 )
				end
			}
		}
	} )
	if ShutdownButton then
		self.ShutdownButton:RegisterAnimationSequences( {
			Splitscreen = {
				{
					function ()
						return self.ShutdownButton:setAnchorsAndPosition( 0, 1, 0, 1, _1080p * 100, _1080p * 600, _1080p * 483, _1080p * 545, 0 )
					end
				}
			}
		} )
	end
	if WarningText then
		self.WarningText:RegisterAnimationSequences( {
			Splitscreen = {
				{
					function ()
						return self.WarningText:setAnchorsAndPosition( 0, 1, 0, 1, _1080p * 118, _1080p * 1820, _1080p * 284.4, _1080p * 345.07, 0 )
					end
				}
			}
		} )
	end
	if WarningTextSP then
		self.WarningTextSP:RegisterAnimationSequences( {
			Splitscreen = {
				{
					function ()
						return self.WarningTextSP:setAnchorsAndPosition( 0, 1, 0, 1, _1080p * 118, _1080p * 1080.16, _1080p * 247.4, _1080p * 292.4, 0 )
					end
				}
			}
		} )
	end
	self._sequences = {
		Splitscreen = function ()
			if ConfirmationText then
				self.ConfirmationText:AnimateSequence( "Splitscreen" )
			end
			if ConfirmationTextSP then
				self.ConfirmationTextSP:AnimateSequence( "Splitscreen" )
			end
			self.LeaveMatchButton:AnimateSequence( "Splitscreen" )
			self.ReturnToMatchButton:AnimateSequence( "Splitscreen" )
			if ShutdownButton then
				self.ShutdownButton:AnimateSequence( "Splitscreen" )
			end
			if WarningText then
				self.WarningText:AnimateSequence( "Splitscreen" )
			end
			if WarningTextSP then
				self.WarningTextSP:AnimateSequence( "Splitscreen" )
			end
		end
	}
	if f0_local2 then
		f0_local2( self, f1_local2, f1_local1 )
	end
	return self
end

if f0_local3 then
	LUI.FlowManager.RegisterStackPushBehaviour( "pause_menu_leave", f0_local3 )
end
if f0_local4 then
	LUI.FlowManager.RegisterStackPushOverBehaviour( "pause_menu_leave", f0_local4 )
end
if f0_local5 then
	LUI.FlowManager.RegisterStackResumeBehaviour( "pause_menu_leave", f0_local5 )
end
if f0_local6 then
	LUI.FlowManager.RegisterStackPopBehaviour( "pause_menu_leave", f0_local6 )
end
