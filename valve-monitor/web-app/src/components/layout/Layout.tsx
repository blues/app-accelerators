import { ReactNode, useEffect, useState } from "react";
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
  const [showLargeLogo, setShowLargeLogo] = useState<boolean>(true);

  useEffect(() => {
    const checkViewportSize = () => {
      if (window.innerWidth >= 768) {
        setShowLargeLogo(true);
      } else if (window.innerWidth < 768) {
        setShowLargeLogo(false);
      }
    };

    window.addEventListener("resize", checkViewportSize);

    /* needs to be called immediately so on initial page load 
    so it will lay out correctly regardless of size  */
    checkViewportSize();

    return () => {
      window.removeEventListener("resize", checkViewportSize);
    };
  }, []);

  return (
    <Layout>
      <Header showLargeLogo={showLargeLogo} />
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
