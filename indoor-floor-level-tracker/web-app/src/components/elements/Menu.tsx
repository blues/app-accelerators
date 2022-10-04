/* eslint-disable @typescript-eslint/no-floating-promises */
import { Dispatch, ReactNode, Key, SetStateAction } from "react";
import { Menu } from "antd";
import type { MenuProps, MenuTheme } from "antd";
import Image from "next/image";
import { useRouter } from "next/router";
import OverviewIcon from "./images/overview.svg";
import SettingsIcon from "./images/settings.svg";
import OverviewIconWhite from "./images/overview_white.svg";
import SettingsIconWhite from "./images/settings_white.svg";

type MenuMode = "vertical" | "horizontal" | "inline";

const MenuComponent = ({
  selectedPage,
  setSelectedPage,
  isSiderPresent,
  menuMode,
  menuTheme,
}: {
  selectedPage: string;
  setSelectedPage: Dispatch<SetStateAction<string>>;
  isSiderPresent: boolean;
  menuTheme: MenuTheme;
  menuMode: MenuMode;
}) => {
  const router = useRouter();

  type MenuItem = Required<MenuProps>["items"][number];
  const Overview = (
    <Image
      src={isSiderPresent ? OverviewIcon : OverviewIconWhite}
      alt="Overview"
    />
  );
  const Settings = (
    <Image
      src={isSiderPresent ? SettingsIcon : SettingsIconWhite}
      alt="Settings"
    />
  );

  function getItem(label: ReactNode, key: Key, icon?: ReactNode): MenuItem {
    return {
      key,
      label,
      icon,
    } as MenuItem;
  }

  const onClick: MenuProps["onClick"] = (e) => {
    setSelectedPage(e.key);
    if (e.key.includes("settings")) {
      router.push("/settings");
    } else {
      router.push("/");
    }
  };

  const menuItems: MenuItem[] = [
    getItem(<div>Overview</div>, "overview", Overview),
    getItem(<div>Settings</div>, "settings", Settings),
  ];

  return (
    <Menu
      onClick={onClick}
      mode={menuMode}
      theme={menuTheme}
      selectedKeys={[selectedPage]}
      items={menuItems}
    />
  );
};

export default MenuComponent;
