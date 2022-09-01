import React from "react";
import Image from "next/image";
import Link from "next/link";
import { Layout } from "antd";
import Logo from "../../../public/images/app_logo.svg";
import styles from "../../styles/Header.module.scss";
import config from "../../../Config";

const HeaderComponent = () => {
  const { Header } = Layout;
  return (
    <Header className={styles.header}>
      <Link href="/">
        <a data-testid="logo">
          <Image src={Logo} height={70} width={100} alt="App Logo" />
        </a>
      </Link>
      <h1 data-testid="company-name" className={styles.headerText}>
        {config.companyName}
      </h1>
    </Header>
  );
};

export default HeaderComponent;
