import { ReactNode } from "react";
import { Layout, Spin } from "antd";
import Header from "./Header";
import Footer from "./Footer";
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
      <Spin className={styles.loader} spinning={isLoading} size="large">
        <div className={styles.mainContentWrapper}>
          <Content className={styles.mainContent}>{children}</Content>
        </div>
      </Spin>
      <Footer />
    </Layout>
  );
};

export default LayoutComponent;
