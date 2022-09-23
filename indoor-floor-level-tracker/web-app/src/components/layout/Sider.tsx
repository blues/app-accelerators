import { ReactNode, Key } from "react";
import Image from "next/image";
import Link from "next/link";
import { Layout, Menu } from "antd";
import type { MenuProps } from "antd";
import LogoIcon from "../../../public/images/logomark-placeholder.svg";
import OverviewIcon from "../elements/images/overview.svg";
import SettingsIcon from "../elements/images/settings.svg";
import styles from "../../styles/Sider.module.scss";

const SiderComponent = ({
  isSiderCollapsed,
}: {
  isSiderCollapsed: boolean;
}) => {
  const { Sider } = Layout;

  type MenuItem = Required<MenuProps>["items"][number];
  const Overview = <Image src={OverviewIcon} alt="Overview" />;
  const Settings = <Image src={SettingsIcon} alt="Settings" />;

  function getItem(label: ReactNode, key: Key, icon?: ReactNode): MenuItem {
    return {
      key,
      icon,
      label,
    } as MenuItem;
  }

  const menuItems: MenuItem[] = [
    getItem(
      <div
        className={`${isSiderCollapsed ? `${styles.isSiderCollapsed}` : ""}`}
      >
        Overview
      </div>,
      "1",
      Overview
    ),
    getItem(
      <div
        className={`${isSiderCollapsed ? `${styles.isSiderCollapsed}` : ""}`}
      >
        Settings
      </div>,
      "2",
      Settings
    ),
  ];

  return (
    <Sider
      trigger={null}
      collapsible
      collapsed={isSiderCollapsed}
      className={styles.siderWrapper}
      theme="light"
      width={180}
    >
      <div className={styles.siderHeader}>
        <Link href="/">
          <a className={styles.siderTitleWrapper} data-testid="logo">
            <Image src={LogoIcon} height={40} width={40} alt="Logo" />
            <p
              className={`${styles.siderTitle} ${
                isSiderCollapsed ? `${styles.collapsed}` : ""
              }`}
            >
              Company Logo
            </p>
          </a>
        </Link>
      </div>
      <Menu mode="inline" defaultSelectedKeys={["1"]} items={menuItems} />
    </Sider>
  );
};

export default SiderComponent;
