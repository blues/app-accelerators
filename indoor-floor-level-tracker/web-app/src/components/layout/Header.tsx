import Image from "next/image";
import { Layout } from "antd";
import config from "../../../Config";
import styles from "../../styles/Header.module.scss";

const HeaderComponent = () => {
  const { Header } = Layout;
  return (
    <Header className={styles.header}>
      {/* <Image src={Logo} height={70} width={100} alt="App Logo" /> */}
      Menu collapser
      <h1 className={styles.headerTitle}>Indoor Floor Level Tracker</h1>
      <h2 data-testid="company-name" className={styles.headerText}>
        {config.companyName}
      </h2>
    </Header>
  );
};

export default HeaderComponent;
