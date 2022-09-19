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
      <Header />
      <Layout>
        <Sider />
        <div className={styles.mainContentWrapper}>
          <LoadingSpinner isLoading={isLoading}>
            <Layout>
              <Content className={styles.mainContent}>{children}</Content>
            </Layout>
          </LoadingSpinner>
          <Footer />
        </div>
      </Layout>
    </Layout>
  );
};

export default LayoutComponent;
