import { ReactNode, useState } from "react";
import { Layout } from "antd";
import Header from "./Header";
import Sider from "./Sider";
import Footer from "./Footer";

import { LoadingSpinner } from "./LoadingSpinner";
import styles from "../../styles/Layout.module.scss";

const LayoutComponent = ({
  children,
  isLoading,
}: {
  children: ReactNode;
  isLoading: boolean;
}) => {
  const { Content } = Layout;
  const [collapsed, setCollapsed] = useState(false);

  const toggleCollapse = (collapse: boolean) => {
    setCollapsed(collapse);
  };

  return (
    <Layout>
      <Sider collapsed={collapsed} />
      <Layout>
        <Header collapsed={collapsed} toggleCollapse={toggleCollapse} />
        <div className={styles.mainContentWrapper}>
          <LoadingSpinner isLoading={isLoading}>
            <Content className={styles.mainContent}>{children}</Content>
          </LoadingSpinner>
          <Footer />
        </div>
      </Layout>
    </Layout>
  );
};

export default LayoutComponent;
