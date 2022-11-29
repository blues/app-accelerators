import React from "react";
import Image from "next/image";
import Link from "next/link";
import { Layout } from "antd";
import Logo from "../../../public/images/logo-placeholder.svg";
import config from "../../../Config";
import styles from "../../styles/Header.module.scss";

const HeaderComponent = () => {
  const { Header } = Layout;
  return (
    <Header className={styles.header}>
      <Link href="/">
        <a data-testid="logo">
          <Image src={Logo} height={60} width={154} alt="App Logo" />
        </a>
      </Link>
      <h1 className={styles.headerTitle}>Valve Monitor</h1>
      <h2 data-testid="company-name" className={styles.headerText}>
        {config.companyName}
      </h2>
    </Header>
  );
};

export default HeaderComponent;
