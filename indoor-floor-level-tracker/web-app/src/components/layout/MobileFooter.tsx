import { Dispatch, SetStateAction } from "react";
import { Layout } from "antd";
import styles from "../../styles/MobileFooter.module.scss";
import MenuComponent from "../elements/Menu";

const MobileFooterComponent = ({
  selectedPage,
  setSelectedPage,
}: {
  selectedPage: string;
  setSelectedPage: Dispatch<SetStateAction<string>>;
}) => {
  const { Footer } = Layout;

  return (
    <div className={styles.mobileFooterWrapper}>
      <Footer className={styles.mobileFooter}>
        <MenuComponent
          selectedPage={selectedPage}
          setSelectedPage={setSelectedPage}
          menuMode="horizontal"
          menuTheme="dark"
        />
      </Footer>
    </div>
  );
};

export default MobileFooterComponent;
