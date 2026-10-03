if game:issingleplayer() or not Engine.InFrontend() then
	return
end

-- A LUI restart rebuilds the Lua catalog and readiness model while the native
-- SKU cache survives. Use the same cache refresh as DwDataUtils.Init.
if Engine.Inventory_AreSKUsFetched() then
	local controller = Engine.GetFirstActiveController()
	DwDataUtils.UpdateData[DwDataUtils.Vendor.Quartermaster]( controller )
	DataSources.inGame.HUD.DwDataUtils.skuDataFetchTracker:SetValue(
		controller, DwDataUtils.AllSKUDataFetchCallTracker.Done )
end

local stock_purchase_choices = QuarterMasterUtils.GetSkuPurchaseChoices

local function purchase( menu, event )
	local scoped = LUI.FlowManager.GetScopedData( menu )
	local state = scoped.s2xPurchase
	if not state then
		state = {}
		scoped.s2xPurchase = state
		local stock_inventory = menu.m_eventHandlers.inventory
		menu:registerEventHandler( "inventory", function ( element, inventory_event )
			if inventory_event.inventoryEventType == InventoryEventType.TaskCompleted and
				inventory_event.inventoryTaskType == InventoryTaskType.PurchaseSKUS then
				-- Native 0x275360 supplies the original ClientTx as trID. Unrelated or
				-- repeated completions must not unlock this menu or play purchase FX.
				if not state.transaction or inventory_event.trID ~= state.transaction then
					return
				end
				state.transaction = nil
				if inventory_event.success == true then
					local animation = "buy_rsd"
					if scoped.skuDetails.skuInfo.SupplyDropType == QuarterMasterUtils.SupplyDropTypeTag.ASD_ZOMBIE then
						animation = animation .. "_zm"
					end
					if not Engine.IsZombiesMode() then
						Character_Scene.PlayVendorAnim( animation, "idle" )
					end
					QuarterMasterUtils.playSkuPurchaseSfx( element )
					ACTIONS.AnimateSequence( element.skuDetailsWidget, "purchaseFX" )
					ACTIONS.AnimateSequence( element, "Purchase" )
				else
					-- No success animation will finish the stock two-part input lock.
					scoped.inputLock.animation = true
					ACTIONS.PlayErrorSound( element )
				end
			end
			-- Keep the game's wallet refresh, breadcrumbs, choices and input unlock.
			return stock_inventory( element, inventory_event )
		end )
	end
	if state.transaction then
		return
	end

	-- The stock detail-screen callback plays success FX immediately and discards
	-- this transaction ID. Only defer presentation; the native pump owns retries.
	local transaction = Engine.Inventory_PurchaseSKU( event.controller,
		scoped.skuDetails.skuInfo.productID, 1 )
	if not transaction or transaction == "" then
		ACTIONS.PlayErrorSound( menu )
		return
	end
	state.transaction = transaction
	scoped.inputLock.dw = false
	scoped.inputLock.animation = false
	ACTIONS.SetInputEnabled( menu, false )
end

QuarterMasterUtils.GetSkuPurchaseChoices = function ( currency, available, affordable, callback, ... )
	local wrapped_callback = callback
	if callback then
		wrapped_callback = function ( menu, event )
			if menu.id == "quartermaster_sku_details" then
				return purchase( menu, event )
			end
			return callback( menu, event )
		end
	end
	return stock_purchase_choices( currency, available, affordable, wrapped_callback, ... )
end

-- The stock CWL preview hardcodes a COD Points glyph even for an AC-priced
-- SKU. Keep its models/materials and only select the glyph from the price.
local stock_cwl_preview = LUI.MenuBuilder.m_types_build.cwl_preview
LUI.MenuBuilder.m_types_build.cwl_preview = function ( menu, controller, ... )
	local preview = stock_cwl_preview( menu, controller, ... )
	preview:SubscribeToModelThroughElement( preview, "id", function ()
		local source, controller_index = preview:GetDataSource()
		local id = source and source.id:GetValue( controller_index )
		local sku = id and Engine.Inventory_GetSKUInfo( id )
		local currency = sku and sku.prices and sku.prices[1] and sku.prices[1].currency
		preview.CodPoints:setImage( RegisterMaterial(
			currency == 6 and "s2_armory_credits_icon_lg" or "cod_points" ), 0 )
	end )
	return preview
end
