import { ReactNode } from "react";
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
  return (
    <Layout>
      <Sider />
      <Layout>
        <Header />
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
