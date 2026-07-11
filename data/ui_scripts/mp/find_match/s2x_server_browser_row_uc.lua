return {
	PostLoadFunc = function ( f1_arg0, f1_arg1, f1_arg2 )
		assert( f1_arg0.HostName )
		assert( f1_arg0.Status )
		assert( f1_arg0.MapName )
		assert( f1_arg0.Players )
		assert( f1_arg0.Mode )
		assert( f1_arg0.Ping )
		if f1_arg2.buttonText then
			f1_arg0.HostName:setText( f1_arg2.buttonText, 0 )
		end
		f1_arg0.RowTextWidgets = {
			f1_arg0.HostName,
			f1_arg0.Status,
			f1_arg0.MapName,
			f1_arg0.Players,
			f1_arg0.Mode,
			f1_arg0.Ping
		}
		f1_arg0:addEventHandler( "button_over", function ()
			if f1_arg2.buttonOverFunc then
				f1_arg2.buttonOverFunc( f1_arg0, event )
			end
			for f2_local0 = 1, #f1_arg0.RowTextWidgets, 1 do
				f1_arg0.RowTextWidgets[f2_local0]:setFont( FONTS.BodyBoldFont.Font )
				f1_arg0.RowTextWidgets[f2_local0]:setFontSize( 30, 0 )
			end
			f1_arg0.Border:setAlpha( 1, 0 )
			if f1_arg0._scoped and f1_arg0._scoped.s2xServerBrowser then
				f1_arg0._scoped.s2xServerBrowser.lastFocusedElement = f1_arg0.populated and "serverButton" or nil
			end
		end )
		f1_arg0:addEventHandler( "button_over_disable", function ()
			if f1_arg2.buttonOverDisableFunc then
				f1_arg2.buttonOverDisableFunc( f1_arg0, event )
			end
			for f3_local0 = 1, #f1_arg0.RowTextWidgets, 1 do
				f1_arg0.RowTextWidgets[f3_local0]:setFont( FONTS.BodyFont.Font )
				f1_arg0.RowTextWidgets[f3_local0]:setFontSize( 28, 0 )
			end
			f1_arg0.Border:setAlpha( 1, 0 )
		end )
		f1_arg0:addEventHandler( "button_disable", function ()
			for f4_local0 = 1, #f1_arg0.RowTextWidgets, 1 do
				f1_arg0.RowTextWidgets[f4_local0]:setFont( FONTS.BodyFont.Font )
				f1_arg0.RowTextWidgets[f4_local0]:setFontSize( 28, 0 )
			end
			f1_arg0.Border:setAlpha( 0, 0 )
		end )
		f1_arg0:addEventHandler( "button_up", function ()
			for f5_local0 = 1, #f1_arg0.RowTextWidgets, 1 do
				f1_arg0.RowTextWidgets[f5_local0]:setFont( FONTS.BodyFont.Font )
				f1_arg0.RowTextWidgets[f5_local0]:setFontSize( 28, 0 )
			end
			f1_arg0.Border:setAlpha( 0, 0 )
		end )
	end
	
}
