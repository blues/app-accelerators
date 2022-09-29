import { ReactNode, Key } from "react";
import Image from "next/image";
import { Layout, Menu } from "antd";
import type { MenuProps } from "antd";
import OverviewIconWhite from "../elements/images/overview_white.svg";
import SettingsIconWhite from "../elements/images/settings_white.svg";
import styles from "../../styles/MobileFooter.module.scss";

const MobileFooterComponent = () => {
  const { Footer } = Layout;

  type MenuItem = Required<MenuProps>["items"][number];
  const Overview = <Image src={OverviewIconWhite} alt="Overview" />;
  const Settings = <Image src={SettingsIconWhite} alt="Settings" />;

  function getItem(label: ReactNode, key: Key, icon?: ReactNode): MenuItem {
    return {
      key,
      icon,
      label,
    } as MenuItem;
  }

  const menuItems: MenuItem[] = [
    getItem("Overview", "1", Overview),
    getItem("Settings", "2", Settings),
  ];

  return (
    <div className={styles.mobileFooterWrapper}>
      <Footer className={styles.mobileFooter}>
        <Menu
          mode="horizontal"
          theme="dark"
          defaultSelectedKeys={["1"]}
          items={menuItems}
        />
      </Footer>
    </div>
  );
};

export default MobileFooterComponent;
