local f0_local0 = require( "s2x_server_browser_row_uc" )
local f0_local1, f0_local2, f0_local3, f0_local4, f0_local5, f0_local6 = nil
if f0_local0 ~= nil and type( f0_local0 ) == "table" then
	f0_local1 = f0_local0.PreLoadFunc
	f0_local2 = f0_local0.PostLoadFunc
	f0_local3 = f0_local0.PushFunc
	f0_local4 = f0_local0.PushOverFunc
	f0_local5 = f0_local0.ResumeFunc
	f0_local6 = f0_local0.PopFunc
end
local s2x_menu_builders = nil
if LUI and LUI.MenuBuilder then
	s2x_menu_builders = LUI.MenuBuilder.m_types_build
end
if s2x_menu_builders == nil then
	s2x_menu_builders = m_types_build
end
assert( type( s2x_menu_builders ) == "table", "[S2x] Could not find MenuBuilder build table" )

s2x_menu_builders["s2x_server_browser_row"] = function ( f1_arg0, f1_arg1 )
	local f1_local0 = {
		left = 0 * _1080p,
		right = 1690 * _1080p,
		top = 0 * _1080p,
		bottom = 40 * _1080p,
		leftAnchor = true,
		rightAnchor = false,
		topAnchor = true,
		bottomAnchor = false
	}
	local self = nil
	if f1_arg1.disableInteractivity == true then
		self = LUI.UIElement.new( f1_local0 )
	else
		self = LUI.UIButton.new( f1_local0 )
	end
	self.id = "s2x_server_browser_row"
	local f1_local2 = f1_arg1 or {}
	local f1_local3 = f1_local2.controllerIndex
	if not f1_local3 then
		if Engine.InFrontend() then
			local f1_local4 = LUI.FlowManager.GetScopedData( self )
			assert( f1_local4 )
			f1_local3 = f1_local4.exclusiveControllerIndex
		else
			f1_local3 = self:getRootController()
		end
	end
	local f1_local4 = nil
	if f1_local2.isBuildChild == true then
		f1_local4 = table.create( 0, 6 )
	end
	if f0_local1 then
		f0_local1( self, f1_local3, f1_local2 )
	end
	self:makeFocusable()
	local f1_local5 = self
	local BackgroundMain = nil
	
	BackgroundMain = LUI.UIImage.new()
	BackgroundMain.id = "BackgroundMain"
	self:addElement( BackgroundMain )
	self.BackgroundMain = BackgroundMain
	
	BackgroundMain:setAlpha( 0, 0 )
	BackgroundMain:setAnchors( 0, 0, 0, 0, 0 )
	BackgroundMain:setBottom( _1080p * 0, 0 )
	BackgroundMain:setLeft( _1080p * 0, 0 )
	BackgroundMain:setRGBFromInt( 0xFF07, 0 )
	BackgroundMain:setRight( _1080p * 0, 0 )
	BackgroundMain:setTop( _1080p * 0, 0 )
	local BackgroundAlt = nil
	
	BackgroundAlt = LUI.UIImage.new()
	BackgroundAlt.id = "BackgroundAlt"
	self:addElement( BackgroundAlt )
	self.BackgroundAlt = BackgroundAlt
	
	BackgroundAlt:setAlpha( 0, 0 )
	BackgroundAlt:setAnchors( 0, 0, 0, 0, 0 )
	BackgroundAlt:setBottom( _1080p * 0, 0 )
	BackgroundAlt:setLeft( _1080p * 0, 0 )
	BackgroundAlt:setRGBFromInt( 0x393C3D, 0 )
	BackgroundAlt:setRight( _1080p * 0, 0 )
	BackgroundAlt:setTop( _1080p * 0, 0 )
	local ColumnLeft = nil
	
	ColumnLeft = LUI.UIImage.new()
	ColumnLeft.id = "ColumnLeft"
	self:addElement( ColumnLeft )
	self.ColumnLeft = ColumnLeft
	
	ColumnLeft:setAlpha( 0, 0 )
	ColumnLeft:setAnchors( 0, 0, 0, 0, 0 )
	ColumnLeft:setBottom( _1080p * 0, 0 )
	ColumnLeft:setLeft( _1080p * 450, 0 )
	ColumnLeft:setRGBFromInt( 0x0, 0 )
	ColumnLeft:setRight( _1080p * -1010, 0 )
	ColumnLeft:setTop( _1080p * 0, 0 )
	local ColumnRight = nil
	
	ColumnRight = LUI.UIImage.new()
	ColumnRight.id = "ColumnRight"
	self:addElement( ColumnRight )
	self.ColumnRight = ColumnRight
	
	ColumnRight:setAlpha( 0, 0 )
	ColumnRight:setAnchors( 0, 0, 0, 0, 0 )
	ColumnRight:setBottom( _1080p * 0, 0 )
	ColumnRight:setLeft( _1080p * 1010, 0 )
	ColumnRight:setRGBFromInt( 0x0, 0 )
	ColumnRight:setRight( _1080p * -500, 0 )
	ColumnRight:setTop( _1080p * 0, 0 )
	local Mode = nil
	
	Mode = LUI.UIMarqueeText.new( nil, {
		controllerIndex = f1_local3,
		fontIconSet = f1_local2.fontIconSet,
		marqueeAxis = LUI.DIRECTION.horizontal
	} )
	Mode.id = "Mode"
	self:addElement( Mode )
	self.Mode = Mode
	
	if f1_local2.fontIconSet ~= nil then
		Mode:setFontIconSet( f1_local2.fontIconSet )
	end
	Mode:setAnchors( 1, 0, 0, 0, 0 )
	Mode:setBottom( _1080p * -2.5, 0 )
	Mode:setFont( FONTS.BodyFont.Font )
	Mode:setFontSize( 24, 0 )
	Mode:setHorizontalAlignment( LUI.HorizontalAlignment.Center )
	Mode:setLeft( _1080p * -470, 0 )
	Mode:setRGBFromInt( SWATCHES.Menus.MenuOffWhite, 0 )
	Mode:setRight( _1080p * -170, 0 )
	Mode:setSpeed( 20 )
	Mode:setText( Engine.Localize( "MENU_NEW" ), 0 )
	Mode:setTop( _1080p * 2.5, 0 )
	Mode:setVerticalAlignment( LUI.VerticalAlignment.Middle )
	local Ping = nil
	
	Ping = LUI.UIText.new()
	Ping.id = "Ping"
	self:addElement( Ping )
	self.Ping = Ping
	
	if f1_local2.fontIconSet ~= nil then
		Ping:setFontIconSet( f1_local2.fontIconSet )
	end
	Ping:setAnchors( 1, 0, 0, 0, 0 )
	Ping:setBottom( _1080p * -2.5, 0 )
	Ping:setFont( FONTS.BodyFont.Font )
	Ping:setFontSize( 24, 0 )
	Ping:setHorizontalAlignment( LUI.HorizontalAlignment.Center )
	Ping:setLeft( _1080p * -150, 0 )
	Ping:setRGBFromInt( SWATCHES.Menus.MenuOffWhite, 0 )
	Ping:setRight( _1080p * -4, 0 )
	Ping:setText( Engine.Localize( "MENU_NEW" ), 0 )
	Ping:setTop( _1080p * 2.5, 0 )
	Ping:setVerticalAlignment( LUI.VerticalAlignment.Middle )
	local Players = nil
	
	Players = LUI.UIText.new()
	Players.id = "Players"
	self:addElement( Players )
	self.Players = Players
	
	if f1_local2.fontIconSet ~= nil then
		Players:setFontIconSet( f1_local2.fontIconSet )
	end
	Players:setAnchors( 0, 1, 0, 0, 0 )
	Players:setBottom( _1080p * -2.5, 0 )
	Players:setFont( FONTS.BodyFont.Font )
	Players:setFontSize( 24, 0 )
	Players:setHorizontalAlignment( LUI.HorizontalAlignment.Center )
	Players:setLeft( _1080p * 1010, 0 )
	Players:setRGBFromInt( SWATCHES.Menus.MenuOffWhite, 0 )
	Players:setRight( _1080p * 1190, 0 )
	Players:setText( Engine.Localize( "MENU_NEW" ), 0 )
	Players:setTop( _1080p * 2.5, 0 )
	Players:setVerticalAlignment( LUI.VerticalAlignment.Middle )
	local MapName = nil
	
	MapName = LUI.UIText.new()
	MapName.id = "MapName"
	self:addElement( MapName )
	self.MapName = MapName
	
	if f1_local2.fontIconSet ~= nil then
		MapName:setFontIconSet( f1_local2.fontIconSet )
	end
	MapName:setAnchors( 0, 1, 0, 0, 0 )
	MapName:setBottom( _1080p * -2.5, 0 )
	MapName:setFont( FONTS.BodyFont.Font )
	MapName:setFontSize( 24, 0 )
	MapName:setHorizontalAlignment( LUI.HorizontalAlignment.Center )
	MapName:setLeft( _1080p * 700, 0 )
	MapName:setRGBFromInt( SWATCHES.Menus.MenuOffWhite, 0 )
	MapName:setRight( _1080p * 990, 0 )
	MapName:setText( Engine.Localize( "MENU_NEW" ), 0 )
	MapName:setTop( _1080p * 2.5, 0 )
	MapName:setupAutoScaleText()
	MapName:setVerticalAlignment( LUI.VerticalAlignment.Middle )
	local Status = nil
	
	Status = LUI.UIText.new()
	Status.id = "Status"
	self:addElement( Status )
	self.Status = Status
	
	if f1_local2.fontIconSet ~= nil then
		Status:setFontIconSet( f1_local2.fontIconSet )
	end
	Status:setAnchors( 0, 1, 0, 0, 0 )
	Status:setBottom( _1080p * -2.5, 0 )
	Status:setFont( FONTS.BodyFont.Font )
	Status:setFontSize( 24, 0 )
	Status:setHorizontalAlignment( LUI.HorizontalAlignment.Center )
	Status:setLeft( _1080p * 470, 0 )
	Status:setRGBFromInt( SWATCHES.Menus.MenuOffWhite, 0 )
	Status:setRight( _1080p * 680, 0 )
	Status:setText( Engine.Localize( "MENU_NEWGAME" ), 0 )
	Status:setTop( _1080p * 2.5, 0 )
	Status:setVerticalAlignment( LUI.VerticalAlignment.Middle )
	local HostName = nil
	
	HostName = LUI.UIText.new()
	HostName.id = "HostName"
	self:addElement( HostName )
	self.HostName = HostName
	
	if f1_local2.fontIconSet ~= nil then
		HostName:setFontIconSet( f1_local2.fontIconSet )
	end
	HostName:setAnchors( 0, 1, 0, 0, 0 )
	HostName:setBottom( _1080p * -2.5, 0 )
	HostName:setFont( FONTS.BodyFont.Font )
	HostName:setFontSize( 24, 0 )
	HostName:setHorizontalAlignment( LUI.HorizontalAlignment.Left )
	HostName:setLeft( _1080p * 4, 0 )
	HostName:setRGBFromInt( SWATCHES.Menus.MenuOffWhite, 0 )
	HostName:setRight( _1080p * 450, 0 )
	HostName:setText( Engine.Localize( "MENU_NEW" ), 0 )
	HostName:setTop( _1080p * 2.5, 0 )
	HostName:setVerticalAlignment( LUI.VerticalAlignment.Middle )
	local Border = nil
	
	Border = LUI.MenuBuilder.BuildRegisteredType( "GenericBorderFrame", {
		controllerIndex = f1_local3,
		fontIconSet = f1_local2.fontIconSet,
		disableInteractivity = f1_local2.disableInteractivity == true
	} )
	Border.id = "Border"
	self:addElement( Border )
	self.Border = Border
	
	Border:setAlpha( 0, 0 )
	Border:setAnchors( 0, 0, 0, 0, 0 )
	Border:setBottom( _1080p * 0, 0 )
	Border:setLeft( _1080p * 0, 0 )
	Border:setRGBFromInt( SWATCHES.Button.MenuCream, 0 )
	Border:setRight( _1080p * 0, 0 )
	Border:setTop( _1080p * 0, 0 )
	if Border.Bottom then
		Border.Bottom:setRGBFromInt( SWATCHES.Button.MenuCream, 0 )
		Border.Bottom:setTop( _1080p * -1, 0 )
	end
	if Border.Left then
		Border.Left:setRGBFromInt( SWATCHES.Button.MenuCream, 0 )
		Border.Left:setRight( _1080p * 1, 0 )
	end
	if Border.Right then
		Border.Right:setLeft( _1080p * -1, 0 )
		Border.Right:setRGBFromInt( SWATCHES.Button.MenuCream, 0 )
	end
	if Border.Top then
		Border.Top:setBottom( _1080p * 1, 0 )
		Border.Top:setRGBFromInt( SWATCHES.Button.MenuCream, 0 )
	end
	self.BackgroundAlt:RegisterAnimationSequences( {
		AlternateLayout = {
			{
				function ()
					return self.BackgroundAlt:setAlpha( 0.59, 0 )
				end
			}
		},
		RegularLayout = {
			{
				function ()
					return self.BackgroundAlt:setAlpha( 0, 0 )
				end
			}
		}
	} )
	self.BackgroundMain:RegisterAnimationSequences( {
		RegularLayout = {
			{
				function ()
					return self.BackgroundMain:setAlpha( 0.65, 0 )
				end
			},
			{
				function ()
					return self.BackgroundMain:setRGBFromInt( 0x292B2B, 0 )
				end
			}
		}
	} )
	self.Border:RegisterAnimationSequences( {
		ButtonOver = {
			{
				function ()
					return self.Border:setAlpha( 1, 0 )
				end
			}
		},
		ButtonUp = {
			{
				function ()
					return self.Border:setAlpha( 0, 0 )
				end
			}
		}
	} )
	self.ColumnLeft:RegisterAnimationSequences( {
		AlternateLayout = {
			{
				function ()
					return self.ColumnLeft:setAlpha( 0.2, 0 )
				end
			}
		},
		RegularLayout = {
			{
				function ()
					return self.ColumnLeft:setAlpha( 0.2, 0 )
				end
			}
		}
	} )
	self.ColumnRight:RegisterAnimationSequences( {
		AlternateLayout = {
			{
				function ()
					return self.ColumnRight:setAlpha( 0.2, 0 )
				end
			}
		},
		RegularLayout = {
			{
				function ()
					return self.ColumnRight:setAlpha( 0.2, 0 )
				end
			}
		}
	} )
	self.HostName:RegisterAnimationSequences( {
		AlternateLayout = {
			{
				function ()
					return self.HostName:setAlpha( 1, 0 )
				end
			}
		},
		ButtonOver = {
			{
				function ()
					return self.HostName:setRGBFromInt( SWATCHES.Menus.MenuGold, 0 )
				end
			},
			{
				function ()
					return self.HostName:setFontSize( 26, 0 )
				end
			}
		},
		ButtonUp = {
			{
				function ()
					return self.HostName:setRGBFromInt( SWATCHES.Button.MenuOffWhite, 0 )
				end
			},
			{
				function ()
					return self.HostName:setFontSize( 24, 0 )
				end
			}
		},
		RegularLayout = {
			{
				function ()
					return self.HostName:setAlpha( 1, 0 )
				end
			}
		}
	} )
	self.MapName:RegisterAnimationSequences( {
		ButtonOver = {
			{
				function ()
					return self.MapName:setRGBFromInt( SWATCHES.Menus.MenuGold, 0 )
				end
			},
			{
				function ()
					return self.MapName:setFontSize( 26, 0 )
				end
			}
		},
		ButtonUp = {
			{
				function ()
					return self.MapName:setRGBFromInt( SWATCHES.Button.MenuOffWhite, 0 )
				end
			},
			{
				function ()
					return self.MapName:setFontSize( 24, 0 )
				end
			}
		}
	} )
	self.Mode:RegisterAnimationSequences( {
		ButtonOver = {
			{
				function ()
					return self.Mode:setRGBFromInt( SWATCHES.Menus.MenuGold, 0 )
				end
			},
			{
				function ()
					return self.Mode:setFontSize( 26, 0 )
				end
			}
		},
		ButtonUp = {
			{
				function ()
					return self.Mode:setRGBFromInt( SWATCHES.Button.MenuOffWhite, 0 )
				end
			},
			{
				function ()
					return self.Mode:setFontSize( 24, 0 )
				end
			}
		}
	} )
	self.Ping:RegisterAnimationSequences( {
		ButtonOver = {
			{
				function ()
					return self.Ping:setRGBFromInt( SWATCHES.Menus.MenuGold, 0 )
				end
			},
			{
				function ()
					return self.Ping:setFontSize( 26, 0 )
				end
			}
		},
		ButtonUp = {
			{
				function ()
					return self.Ping:setRGBFromInt( SWATCHES.Button.MenuOffWhite, 0 )
				end
			},
			{
				function ()
					return self.Ping:setFontSize( 24, 0 )
				end
			}
		}
	} )
	self.Players:RegisterAnimationSequences( {
		ButtonOver = {
			{
				function ()
					return self.Players:setRGBFromInt( SWATCHES.Menus.MenuGold, 0 )
				end
			},
			{
				function ()
					return self.Players:setFontSize( 26, 0 )
				end
			}
		},
		ButtonUp = {
			{
				function ()
					return self.Players:setRGBFromInt( SWATCHES.Button.MenuOffWhite, 0 )
				end
			},
			{
				function ()
					return self.Players:setFontSize( 24, 0 )
				end
			}
		}
	} )
	self.Status:RegisterAnimationSequences( {
		ButtonOver = {
			{
				function ()
					return self.Status:setRGBFromInt( SWATCHES.Menus.MenuGold, 0 )
				end
			},
			{
				function ()
					return self.Status:setFontSize( 26, 0 )
				end
			}
		},
		ButtonUp = {
			{
				function ()
					return self.Status:setRGBFromInt( SWATCHES.Button.MenuOffWhite, 0 )
				end
			},
			{
				function ()
					return self.Status:setFontSize( 24, 0 )
				end
			}
		}
	} )
	self._sequences = {
		AlternateLayout = function ()
			self.BackgroundAlt:AnimateSequence( "AlternateLayout" )
			self.ColumnLeft:AnimateSequence( "AlternateLayout" )
			self.ColumnRight:AnimateSequence( "AlternateLayout" )
			self.HostName:AnimateSequence( "AlternateLayout" )
		end,
		ButtonOver = function ()
			self.Border:AnimateSequence( "ButtonOver" )
			self.HostName:AnimateSequence( "ButtonOver" )
			self.MapName:AnimateSequence( "ButtonOver" )
			self.Mode:AnimateSequence( "ButtonOver" )
			self.Ping:AnimateSequence( "ButtonOver" )
			self.Players:AnimateSequence( "ButtonOver" )
			self.Status:AnimateSequence( "ButtonOver" )
		end,
		ButtonUp = function ()
			self.Border:AnimateSequence( "ButtonUp" )
			self.HostName:AnimateSequence( "ButtonUp" )
			self.MapName:AnimateSequence( "ButtonUp" )
			self.Mode:AnimateSequence( "ButtonUp" )
			self.Ping:AnimateSequence( "ButtonUp" )
			self.Players:AnimateSequence( "ButtonUp" )
			self.Status:AnimateSequence( "ButtonUp" )
		end,
		RegularLayout = function ()
			self.BackgroundAlt:AnimateSequence( "RegularLayout" )
			self.BackgroundMain:AnimateSequence( "RegularLayout" )
			self.ColumnLeft:AnimateSequence( "RegularLayout" )
			self.ColumnRight:AnimateSequence( "RegularLayout" )
			self.HostName:AnimateSequence( "RegularLayout" )
		end
	}
	self:addEventHandler( "button_over", function ( f38_arg0, f38_arg1 )
		ACTIONS.AnimateSequence( self, "ButtonOver" )
	end )
	self:addEventHandler( "button_up", function ( f39_arg0, f39_arg1 )
		ACTIONS.AnimateSequence( self, "ButtonUp" )
	end )
	if f1_local2.isBuildChild == true then
		self:addEventHandler( "grid_cell_empty", function ( f40_arg0, f40_arg1 )
			if self.SetIsOperable then
				self:SetIsOperable( false )
			end
			f1_local4.BackgroundMain = BackgroundMain:getAlpha()
			BackgroundMain:setAlpha( 0 )
			f1_local4.BackgroundAlt = BackgroundAlt:getAlpha()
			BackgroundAlt:setAlpha( 0 )
			f1_local4.ColumnLeft = ColumnLeft:getAlpha()
			ColumnLeft:setAlpha( 0 )
			f1_local4.ColumnRight = ColumnRight:getAlpha()
			ColumnRight:setAlpha( 0 )
			Mode:setAlpha( 0 )
			Ping:setAlpha( 0 )
			Players:setAlpha( 0 )
			MapName:setAlpha( 0 )
			Status:setAlpha( 0 )
			f1_local4.HostName = HostName:getAlpha()
			HostName:setAlpha( 0 )
			f1_local4.Border = Border:getAlpha()
			Border:setAlpha( 0 )
		end )
	end
	if f1_local2.isBuildChild == true then
		self:addEventHandler( "grid_cell_populated", function ( f41_arg0, f41_arg1 )
			local f41_local0 = f41_arg1.controller or f1_local3
			if self.SetIsOperable then
				self:SetIsOperable( true )
			end
			BackgroundMain:setAlpha( f1_local4.BackgroundMain or 0 )
			BackgroundAlt:setAlpha( f1_local4.BackgroundAlt or 0 )
			ColumnLeft:setAlpha( f1_local4.ColumnLeft or 0 )
			ColumnRight:setAlpha( f1_local4.ColumnRight or 0 )
			Mode:setAlpha( 1 )
			Ping:setAlpha( 1 )
			Players:setAlpha( 1 )
			MapName:setAlpha( 1 )
			Status:setAlpha( 1 )
			HostName:setAlpha( f1_local4.HostName or 1 )
			Border:setAlpha( f1_local4.Border or 0 )
		end )
	end
	if f0_local2 then
		f0_local2( self, f1_local3, f1_local2, f1_local5 )
	end
	return self
end
if f0_local3 then
	LUI.FlowManager.RegisterStackPushBehaviour( "s2x_server_browser_row", f0_local3 )
end
if f0_local4 then
	LUI.FlowManager.RegisterStackPushOverBehaviour( "s2x_server_browser_row", f0_local4 )
end
if f0_local5 then
	LUI.FlowManager.RegisterStackResumeBehaviour( "s2x_server_browser_row", f0_local5 )
end
if f0_local6 then
	LUI.FlowManager.RegisterStackPopBehaviour( "s2x_server_browser_row", f0_local6 )
end
