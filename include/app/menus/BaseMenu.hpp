#pragma once

namespace app::menus {

class BaseMenu {
  public:
    virtual ~BaseMenu() = default;
    virtual const char *menuId() const = 0;
    virtual bool isOpen() const {
        return true;
    }
};

} // namespace app::menus
