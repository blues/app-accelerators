import { createElement } from "react";
import { MenuFoldOutlined, MenuUnfoldOutlined } from "@ant-design/icons";
import { Layout } from "antd";
import config from "../../../Config";
import styles from "../../styles/Header.module.scss";

const HeaderComponent = ({
  isSiderCollapsed,
  toggleCollapse,
  isSiderPresent,
}: {
  isSiderCollapsed: boolean;
  toggleCollapse: (collpase: boolean) => void;
  isSiderPresent: boolean;
}) => {
  const { Header } = Layout;

  return (
    <Header className={styles.header}>
      {createElement(isSiderCollapsed ? MenuUnfoldOutlined : MenuFoldOutlined, {
        className: "trigger",
        onClick: () => toggleCollapse(!isSiderCollapsed),
      })}
      <h1 className={styles.headerTitle}>Indoor Floor Level Tracker</h1>
      <h2
        data-testid="company-name"
        className={`${styles.headerText} ${
          isSiderPresent ? "" : `${styles.hidden}`
        }`}
      >
        {config.companyName}
      </h2>
    </Header>
  );
};

export default HeaderComponent;
