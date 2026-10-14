if game:issingleplayer() or not Engine.InFrontend() then
    return
end

local MENU_NAME = "s2x_mods_menu"

local function has_mod_api()
    return type(mods) == "table" and
        type(mods.getloaded) == "function" and
        type(mods.getlist) == "function" and
        type(mods.getinfo) == "function" and
        type(mods.load) == "function" and
        type(mods.unload) == "function"
end

local function get_controller_index(element, properties)
    if properties and properties.controllerIndex then
        return properties.controllerIndex
    end

    local scoped_data = LUI.FlowManager.GetScopedData(element)
    if scoped_data and scoped_data.exclusiveControllerIndex then
        return scoped_data.exclusiveControllerIndex
    end

    return Engine.GetFirstActiveController()
end

local function get_mod_info(folder)
    if not has_mod_api() then
        return {
            isvalid = false,
            name = folder,
            description = "The native S2x mod API is unavailable.",
            author = "",
            version = "",
        }
    end

    local ok, info = pcall(mods.getinfo, folder)
    if not ok or type(info) ~= "table" then
        return {
            isvalid = false,
            name = folder,
            description = "No description provided.",
            author = "",
            version = "",
        }
    end

    if not info.name or info.name == "" then
        info.name = folder
    end
    if not info.description or info.description == "" then
        info.description = "No description provided."
    end
    return info
end

local function build_options(controller)
    local options = {}

    if not has_mod_api() then
        options[#options + 1] = {
            buttonType = "GenericButton",
            buttonText = "Mod Manager API Unavailable",
            buttonDesc = "The MODS tab loaded, but the native mod API was not registered. Check the S2x build and Lua console.",
            buttonActionFunc = function() end,
        }
        return options
    end

    local loaded_mod = mods.getloaded() or ""

    if loaded_mod ~= "" then
        local active_info = get_mod_info(loaded_mod)
        local active_name = active_info.name or loaded_mod
        options[#options + 1] = {
            buttonType = "GenericButton",
            buttonText = "Unload Current Mod",
            buttonDesc = "Unload " .. active_name .. " and return to the stock S2x scripts.",
            buttonActionFunc = function()
                mods.unload()
            end,
        }
    end

    local mod_list = mods.getlist() or {}
    if #mod_list == 0 then
        options[#options + 1] = {
            buttonType = "GenericButton",
            buttonText = "No Compatible Mods Found",
            buttonDesc = "Add a mod folder under s2x/mods with scripts and/or ui_scripts. Optional meta.json supports mode: mp, zm, or mp, zm.",
            buttonActionFunc = function() end,
        }
        return options
    end

    for i = 1, #mod_list do
        local folder = mod_list[i]
        local info = get_mod_info(folder)
        local display_name = info.name or folder
        local description = info.description or "No description provided."

        if info.author and info.author ~= "" then
            description = description .. "\nAuthor: " .. info.author
        end
        if info.version and info.version ~= "" then
            description = description .. "\nVersion: " .. info.version
        end
        if info.mode and info.mode ~= "" then
            description = description .. "\nMode: " .. info.mode
        end

        local is_loaded = loaded_mod == folder
        if is_loaded then
            description = description .. "\n\nCurrently active. Select another mod or choose Unload Current Mod."
        else
            description = description .. "\n\nPress select to load this mod."
        end

        options[#options + 1] = {
            buttonType = "GenericButton",
            buttonText = (is_loaded and "[ACTIVE] " or "") .. display_name,
            buttonDesc = description,
            buttonActionFunc = function()
                if mods.getloaded() ~= folder then
                    mods.load(folder)
                end
            end,
        }
    end

    return options
end

local function build_mod_menu(properties)
    local self = LUI.UIElement.new({
        left = 0, right = 0, top = 0, bottom = 0,
        leftAnchor = true, rightAnchor = true,
        topAnchor = true, bottomAnchor = true,
    })
    self.id = MENU_NAME
    self:playSound("menu_open")

    properties = properties or {}
    local controller = get_controller_index(self, properties)
    local scoped_data = LUI.FlowManager.GetScopedData(self)
    scoped_data.gridData = build_options(controller)

    local background = LUI.MenuBuilder.BuildRegisteredType("GenericMenuBackground", {
        controllerIndex = controller,
        fontIconSet = properties.fontIconSet,
    })
    background.id = "S2xModsBackground"
    background:setAnchors(0, 0, 0, 0, 0)
    background:setBottom(0, 0)
    background:setLeft(0, 0)
    background:setRight(0, 0)
    background:setTop(0, 0)
    self:addElement(background)

    local helper_bar = LUI.MenuBuilder.BuildRegisteredType("button_helper_bar", {
        controllerIndex = controller,
        fontIconSet = properties.fontIconSet,
    })
    helper_bar.id = "S2xModsButtonHelperBar"
    helper_bar:setAnchors(0, 0, 1, 0, 0)
    helper_bar:setBottom(_1080p * -55, 0)
    helper_bar:setLeft(0, 0)
    helper_bar:setRight(0, 0)
    helper_bar:setTop(_1080p * -105, 0)
    self:addElement(helper_bar)

    local options = LUI.MenuBuilder.BuildRegisteredType("OptionButtonsGrid", {
        controllerIndex = controller,
        fontIconSet = properties.fontIconSet,
        OptionsGrid_maxVisibleRows = 7,
        OptionsGrid_verticalAlignment = LUI.Alignment.Top,
    })
    options.id = "S2xModsOptions"
    options:setAnchors(0, 1, 0, 1, 0)
    options:setBottom(_1080p * 952.08, 0)
    options:setLeft(0, 0)
    options:setRight(_1080p * 900, 0)
    options:setTop(_1080p * 200, 0)
    self:addElement(options)

    self:addEventHandler("menu_active", function()
        options:processEvent({ name = "content_refresh" })
    end)

    local title = LUI.MenuBuilder.BuildRegisteredType("GenericMenuTitle", {
        controllerIndex = controller,
        fontIconSet = properties.fontIconSet,
    })
    title.id = "S2xModsTitle"
    title:setAnchors(0, 0, 0, 1, 0)
    title:setBottom(_1080p * 173, 0)
    title:setLeft(_1080p * 100, 0)
    title:setRight(_1080p * -100, 0)
    title:setTop(_1080p * 125, 0)
    if title.Title then
        title.Title:setFont(FONTS.BodyBoldFont.Font)
        title.Title:setHorizontalAlignment(LUI.HorizontalAlignment.Left)
        title.Title:setText("MODS", 0)
    end
    self:addElement(title)

    local description = LUI.MenuBuilder.BuildRegisteredType("GenericMenuDescription", {
        controllerIndex = controller,
        fontIconSet = properties.fontIconSet,
    })
    description.id = "S2xModsDescription"
    description:setAnchors(0, 1, 0, 1, 0)
    description:setBottom(_1080p * 303, 0)
    description:setLeft(_1080p * 924, 0)
    description:setRight(_1080p * 1820, 0)
    description:setTop(_1080p * 200, 0)
    if description.DescriptionText then
        description.DescriptionText:setText("", 0)
    end
    if description.DescriptionTitle then
        description.DescriptionTitle:setText("", 0)
    end
    self:addElement(description)

    local helper = helper_bar:BeginSet()
    helper = helper:AddBackButton()
    helper = helper:AddLeft(LuaButton.primary, "LUA_MENU_SELECT", nil)
    helper:Finish()

    if title.SetTitle then
        title:SetTitle("MODS", "LUA_MENU_SOLDIER")
    end

    return self
end

LUI.MenuBuilder.registerType(MENU_NAME, function(_, properties)
    return build_mod_menu(properties)
end)

local function get_find_match_state(menu)
    local scoped_data = LUI.FlowManager.GetScopedData(menu)
    return scoped_data and scoped_data.findGameMenu or nil
end

local function get_event_controller(event, fallback_controller)
    if event and type(event.controller) == "number" then
        return event.controller
    end

    return fallback_controller or Engine.GetFirstActiveController()
end

local function create_mod_tile(controller)
    local is_zombies = Engine.IsZombiesMode()
    local image_name = is_zombies and "menu_playlist_select_zm_shattered" or "menu_playlist_select_hq"
    local blur_image_name = is_zombies and "menu_playlist_select_zm_shattered_blur" or
        "menu_playlist_select_hq_blur"

    return {
        image = image_name,
        blurImage = blur_image_name,
        text = "MODS",
        desc = "Load and manage mods for this game mode.",
        actionFunc = function(_, event)
            local controller_index = get_event_controller(event, controller)
            if type(controller_index) ~= "number" then
                return
            end

            AAR.ClearAAR()
            ACTIONS.OpenMenu(MENU_NAME, false, controller_index)
        end,
        s2xModsTile = true,
    }
end

local function contains_mod_tile(buttons)
    for _, button in ipairs(buttons or {}) do
        if button.s2xModsTile then
            return true
        end
    end

    return false
end

local function patch_find_match_menu(menu, properties)
    if not menu then
        return false
    end

    local state = get_find_match_state(menu)
    if not state or not state.topButtons or contains_mod_tile(state.topButtons) or
        contains_mod_tile(state.bottomButtons) then
        return false
    end

    local top_buttons = state.topButtons
    local bottom_buttons = state.bottomButtons or {}
    if #top_buttons < 3 or not top_buttons[1] or not top_buttons[2] then
        return false
    end

    local controller = get_controller_index(menu, properties)
    local mod_tile = create_mod_tile(controller)

    if Engine.IsZombiesMode() then
        if not top_buttons[3] then
            return false
        end

        state.topButtons = {
            top_buttons[1],
            top_buttons[2],
            mod_tile,
            top_buttons[3],
        }
    else
        if not top_buttons[4] then
            return false
        end

        local new_bottom_buttons = {}
        for index = 3, #top_buttons do
            new_bottom_buttons[#new_bottom_buttons + 1] = top_buttons[index]
        end
        for _, button in ipairs(bottom_buttons) do
            new_bottom_buttons[#new_bottom_buttons + 1] = button
        end

        state.topButtons = { top_buttons[1], top_buttons[2], mod_tile }
        state.bottomButtons = new_bottom_buttons
    end

    state.numTotalColumns = 4
    state.numColWindows = 4
    return true
end

s2x_install_mods_find_match_hook = function()
    local menu_builders = LUI.MenuBuilder.m_types_build or m_types_build
    local stock_controller = package.loaded["s2.findgame_menu_uc"]
    if type(menu_builders) ~= "table" or type(stock_controller) ~= "table" or
        type(stock_controller.PreLoadFunc) ~= "function" then
        error("Could not find the stock Find Match controller")
    end
    if stock_controller.s2xModsWrapped then
        return
    end

    local stock_preload = stock_controller.PreLoadFunc
    local stock_builder = menu_builders["findgame_menu"]
    local stock_module = package.loaded["s2.findgame_menu"]
    if type(stock_builder) ~= "function" then
        error("Could not find the stock Find Match menu builder")
    end

    stock_controller.PreLoadFunc = function(menu, controller, properties)
        local result = stock_preload(menu, controller, properties)
        pcall(patch_find_match_menu, menu, { controllerIndex = controller })
        return result
    end

    menu_builders["findgame_menu"] = nil
    package.loaded["s2.findgame_menu"] = nil
    local loaded, load_error = pcall(require, "s2.findgame_menu")
    if not loaded or type(menu_builders["findgame_menu"]) ~= "function" then
        stock_controller.PreLoadFunc = stock_preload
        menu_builders["findgame_menu"] = stock_builder
        package.loaded["s2.findgame_menu"] = stock_module
        error(load_error or "Could not reload the stock Find Match menu builder")
    end

    stock_controller.s2xModsWrapped = true
end
