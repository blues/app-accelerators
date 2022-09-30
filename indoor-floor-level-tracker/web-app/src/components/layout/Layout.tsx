import { ReactNode, useEffect, useState } from "react";
import { Layout } from "antd";
import Header from "./Header";
import Sider from "./Sider";
import Footer from "./Footer";
import MobileFooterComponent from "./MobileFooter";
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
  const [isSiderCollapsed, setIsSiderCollapsed] = useState<boolean>(false);
  const [isSiderPresent, setIsSiderPresent] = useState<boolean>(true);

  useEffect(() => {
    const toggleSidebarVisibility = () => {
      if (window.innerWidth >= 576) {
        setIsSiderPresent(true);
      } else if (window.innerWidth < 576) {
        setIsSiderPresent(false);
      }
    };

    window.addEventListener("resize", toggleSidebarVisibility);

    /* needs to be called immediately so on initial page load 
    so it will lay out correctly regardless of size  */
    toggleSidebarVisibility();

    return () => {
      window.removeEventListener("resize", toggleSidebarVisibility);
    };
  }, []);

  const toggleCollapse = (collapse: boolean) => {
    setIsSiderCollapsed(collapse);
  };

  useEffect(() => {
    const disableSiderBarToggle = () => {
      if (window.innerWidth > 992) {
        setIsSiderCollapsed(false);
      } else if (window.innerWidth < 992) {
        setIsSiderCollapsed(true);
      }
    };

    window.addEventListener("resize", disableSiderBarToggle);

    return () => {
      window.removeEventListener("resize", disableSiderBarToggle);
    };
  }, []);

  return (
    <Layout>
      {isSiderPresent ? <Sider isSiderCollapsed={isSiderCollapsed} /> : null}
      <Layout className={styles.mainContentSection}>
        <Header
          isSiderCollapsed={isSiderCollapsed}
          toggleCollapse={toggleCollapse}
          isSiderPresent={isSiderPresent}
        />
        <div className={styles.mainContentWrapper}>
          <LoadingSpinner isLoading={isLoading}>
            <Content className={styles.mainContent}>{children}</Content>
          </LoadingSpinner>
          {isSiderPresent ? <Footer /> : null}
        </div>
      </Layout>
      {!isSiderPresent ? <MobileFooterComponent /> : null}
    </Layout>
  );
};

export default LayoutComponent;
