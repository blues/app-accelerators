import { ReactNode, Key } from "react";
import Image from "next/image";
import Link from "next/link";
import { Layout, Menu } from "antd";
import type { MenuProps } from "antd";
import LogoIcon from "../../../public/images/logomark-placeholder.svg";
import OverviewIcon from "../elements/images/overview.svg";
import SettingsIcon from "../elements/images/settings.svg";
import styles from "../../styles/Sider.module.scss";

const SiderComponent = () => {
  const { Sider } = Layout;

  type MenuItem = Required<MenuProps>["items"][number];
  const Overview = <Image src={OverviewIcon} alt="Four squares" />;
  const Settings = <Image src={SettingsIcon} alt="Four squares" />;

  function getItem(label: ReactNode, key: Key, icon?: ReactNode): MenuItem {
    return {
      key,
      icon,
      label,
    } as MenuItem;
  }

  const menuItems: MenuItem[] = [
    getItem("Overview", 1, Overview),
    getItem("Settings", 2, Settings),
  ];

  return (
    <Sider className={styles.siderWrapper} theme="light" width={180}>
      <div className={styles.siderHeader}>
        <Link href="/">
          <a className={styles.siderTitleWrapper} data-testid="logo">
            <Image src={LogoIcon} height={40} width={40} alt="Logo" />
            <p className={styles.siderTitle}>Company Logo</p>
          </a>
        </Link>
      </div>
      <Menu
        mode="inline"
        defaultSelectedKeys={["1"]}
        style={{ height: "100%" }}
        items={menuItems}
      />
    </Sider>
  );
};

export default SiderComponent;
