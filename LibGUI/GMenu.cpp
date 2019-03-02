#include <LibGUI/GAction.h>
#include <LibGUI/GMenu.h>
#include <LibGUI/GEventLoop.h>
#include <AK/HashMap.h>

static HashMap<int, GMenu*>& all_menus()
{
    static HashMap<int, GMenu*>* map;
    if (!map)
        map = new HashMap<int, GMenu*>();
    return *map;
}

GMenu* GMenu::from_menu_id(int menu_id)
{
    auto it = all_menus().find(menu_id);
    if (it == all_menus().end())
        return nullptr;
    return (*it).value;
}

GMenu::GMenu(const String& name)
    : m_name(name)
{
}

GMenu::~GMenu()
{
    unrealize_menu();
}

void GMenu::add_action(RetainPtr<GAction>&& action)
{
    m_items.append(make<GMenuItem>(move(action)));
}

void GMenu::add_separator()
{
    m_items.append(make<GMenuItem>(GMenuItem::Separator));
}

int GMenu::realize_menu()
{
    WSAPI_ClientMessage request;
    request.type = WSAPI_ClientMessage::Type::CreateMenu;
    ASSERT(m_name.length() < (ssize_t)sizeof(request.text));
    strcpy(request.text, m_name.characters());
    request.text_length = m_name.length();
    auto response = GEventLoop::main().sync_request(request, WSAPI_ServerMessage::Type::DidCreateMenu);
    m_menu_id = response.menu.menu_id;

    ASSERT(m_menu_id > 0);
    for (int i = 0; i < m_items.size(); ++i) {
        auto& item = *m_items[i];
        if (item.type() == GMenuItem::Separator) {
            WSAPI_ClientMessage request;
            request.type = WSAPI_ClientMessage::Type::AddMenuSeparator;
            request.menu.menu_id = m_menu_id;
            GEventLoop::main().sync_request(request, WSAPI_ServerMessage::Type::DidAddMenuSeparator);
            continue;
        }
        if (item.type() == GMenuItem::Action) {
            auto& action = *item.action();
            WSAPI_ClientMessage request;
            request.type = WSAPI_ClientMessage::Type::AddMenuItem;
            request.menu.menu_id = m_menu_id;
            request.menu.identifier = i;
            ASSERT(action.text().length() < (ssize_t)sizeof(request.text));
            strcpy(request.text, action.text().characters());
            request.text_length = action.text().length();

            if (action.shortcut().is_valid()) {
                auto shortcut_text = action.shortcut().to_string();
                ASSERT(shortcut_text.length() < (ssize_t)sizeof(request.menu.shortcut_text));
                strcpy(request.menu.shortcut_text, shortcut_text.characters());
                request.menu.shortcut_text_length = shortcut_text.length();
            } else {
                request.menu.shortcut_text_length = 0;
            }

            GEventLoop::main().sync_request(request, WSAPI_ServerMessage::Type::DidAddMenuItem);
        }
    }
    all_menus().set(m_menu_id, this);
    return m_menu_id;
}

void GMenu::unrealize_menu()
{
    if (!m_menu_id)
        return;
    all_menus().remove(m_menu_id);
    WSAPI_ClientMessage request;
    request.type = WSAPI_ClientMessage::Type::DestroyMenu;
    request.menu.menu_id = m_menu_id;
    GEventLoop::main().sync_request(request, WSAPI_ServerMessage::Type::DidDestroyMenu);
    m_menu_id = 0;
}

GAction* GMenu::action_at(int index)
{
    if (index >= m_items.size())
        return nullptr;
    return m_items[index]->action();
}
