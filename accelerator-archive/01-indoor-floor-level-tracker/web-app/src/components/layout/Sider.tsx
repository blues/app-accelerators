import { Dispatch, SetStateAction } from "react";
import Link from "next/link";
import Image from "next/image";
import { Layout } from "antd";
import Logo from "../../../public/images/floor-tracker_icon.svg";
import MenuComponent from "../elements/Menu";
import styles from "../../styles/Sider.module.scss";

const SiderComponent = ({
  isSiderCollapsed,
  selectedPage,
  setSelectedPage,
  isSiderPresent,
}: {
  isSiderCollapsed: boolean;
  selectedPage: string;
  setSelectedPage: Dispatch<SetStateAction<string>>;
  isSiderPresent: boolean;
}) => {
  const { Sider } = Layout;

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
            <Image src={Logo} height={40} width={40} alt="Logo" />
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
      <MenuComponent
        selectedPage={selectedPage}
        setSelectedPage={setSelectedPage}
        isSiderPresent={isSiderPresent}
        menuMode="inline"
        menuTheme="light"
      />
    </Sider>
  );
};

export default SiderComponent;
