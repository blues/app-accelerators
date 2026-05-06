import Image from "next/image";
import Link from "next/link";
import { Layout } from "antd";
import Logo from "../../../public/images/logo-placeholder.svg";
import Logomark from "../../../public/images/logomark-placeholder.svg";
import config from "../../../Config";
import styles from "../../styles/Header.module.scss";

const HeaderComponent = () => {
  const { Header } = Layout;
  return (
    <Header className={styles.header}>
      <Link href="/">
        <a className={styles.largeLogo} data-testid="logo">
          <Image src={Logo} height={60} width={154} alt="App Logo" />
        </a>
      </Link>
      <Link href="/">
        <a className={styles.smallLogo} data-testid="smalllogo">
          <Image src={Logomark} height={60} width={37} alt="App Logo" />
        </a>
      </Link>
      <h1 className={styles.headerTitle}>Valve Monitor</h1>
      <h2 data-testid="company-name" className={styles.headerText}>
        {config.companyName}
      </h2>
      <h2 className={styles.headerTextSmall}>BW Demo</h2>
    </Header>
  );
};

export default HeaderComponent;
