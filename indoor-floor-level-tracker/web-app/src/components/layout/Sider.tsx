import { ReactNode, Key, createElement } from "react";
import { Layout, Menu } from "antd";
import type { MenuProps } from "antd";
import styles from "../../styles/Sider.module.scss";

const SiderComponent = () => {
  const { Sider } = Layout;

  type MenuItem = Required<MenuProps>["items"][number];

  function getItem(label: ReactNode, key: Key, icon?: ReactNode): MenuItem {
    return {
      key,
      icon,
      label,
    } as MenuItem;
  }

  const menuItems: MenuItem[] = [
    getItem("Overview", 1, "Squares"),
    getItem("Settings", 2, "Cog"),
  ];

  return (
    <Sider className={styles.siderWrapper} theme="light" width={180}>
      <div className={styles.siderHeader}>
        <p
          style={{
            height: "40px",
            width: "40px",
            marginRight: "auto",
            marginLeft: "auto",
            paddingTop: "21px",
          }}
        >
          Logo
        </p>
        <h2 className={styles.siderTitle}>Fleet Name</h2>
      </div>
      <Menu
        mode="inline"
        defaultSelectedKeys={["1"]}
        style={{ height: "calc(100% - 137px)" }}
        items={menuItems}
      />
    </Sider>
  );
};

export default SiderComponent;
