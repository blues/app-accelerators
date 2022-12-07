import React from "react";
import Image from "next/image";
import Link from "next/link";
import { Layout } from "antd";
import Logo from "../../../public/images/logo-placeholder.svg";
import Logomark from "../../../public/images/logomark-placeholder.svg";
import config from "../../../Config";
import styles from "../../styles/Header.module.scss";

const HeaderComponent = ({ showLargeLogo }) => {
  const { Header } = Layout;
  return (
    <Header className={styles.header}>
      <Link href="/">
        <a data-testid="logo">
          {showLargeLogo ? (
            <Image src={Logo} height={60} width={154} alt="App Logo" />
          ) : (
            <Image src={Logomark} height={60} width={35} alt="App Logo" />
          )}
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
