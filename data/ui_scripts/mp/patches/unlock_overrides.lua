if game:issingleplayer() then
	return
end

local GetDvarBool_og = Engine.GetDvarBool

local function is_unlock_all_enabled()
	return not Engine.IsZombiesMode() and GetDvarBool_og( "cg_unlockall_items" ) == true and
		GetDvarBool_og( "cg_unlockall_loot" ) == true
end

Engine.GetDvarBool = function ( dvar_name, ... )
	-- old dev dvar for unlock all stuff in WWII dump
	if dvar_name == "709" and is_unlock_all_enabled() then
		return true
	end

	return GetDvarBool_og( dvar_name, ... )
end

if InventoryUtils and InventoryUtils.GetUnlockWeaponSpecialCase then
	local GetUnlockWeaponSpecialCase_og = InventoryUtils.GetUnlockWeaponSpecialCase
	InventoryUtils.GetUnlockWeaponSpecialCase = function ( guid, ... )
		if is_unlock_all_enabled() then
			return true
		end

		return GetUnlockWeaponSpecialCase_og( guid, ... )
	end
end

local function get_top_menu_info()
	local root = Engine.GetLuiRoot()
	if not root or not root.flowManager then
		return nil
	end

	return LUI.FlowManager.GetTopMenuInfo( root.flowManager.menuInfoStack )
end

local function open_weapon_variant_menu( controller, storage_category, weapon_index, guid )
	if not is_unlock_all_enabled() or not CONDITIONS.IsSystemLink() or CONDITIONS.UsePickTen() then
		return false
	end

	local menu_info = get_top_menu_info()
	if not menu_info or menu_info.name ~= "cac_edit_weapon" or not menu_info.menu then
		return false
	end

	local menu = menu_info.menu
	if not menu.BaseWeaponList or not menu.BaseWeaponList.WeaponList then
		return false
	end

	local scoped_data = LUI.FlowManager.GetScopedData( menu )
	local data_source = menu.BaseWeaponList.WeaponList:GetDataSource()
	if not scoped_data or not scoped_data.cacEditData or not data_source or data_source.guid:GetValue( controller ) ~= guid then
		return false
	end

	LUI.FlowManager.RequestAddMenu( nil, "cac_edit_weapon_variant", true, controller, false, {
		baseWeaponGuid = guid,
		dataSource = data_source,
		dataSourceController = controller,
		weaponIndex = weapon_index,
		storageCategory = storage_category,
		weaponTypeIndex = scoped_data.cacEditData.weaponTypeIndex
	} )

	ACTIONS.PlaySelectSound()

	return true
end

if Cac and Cac.EquipItem then
	local EquipItem_og = Cac.EquipItem
	Cac.EquipItem = function ( controller, storage_category, weapon_index, class_loc, class_idx, guid, ... )
		if open_weapon_variant_menu( controller, storage_category, weapon_index, guid ) then
			return
		end

		return EquipItem_og( controller, storage_category, weapon_index, class_loc, class_idx, guid, ... )
	end
end
