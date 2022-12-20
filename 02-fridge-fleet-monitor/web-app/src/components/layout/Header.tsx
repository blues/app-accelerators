import React from "react";
import Image from "next/image";
import Link from "next/link";
import { Layout } from "antd";
import Logo from "../../../public/images/fridge-temp_icon.svg";
import config from "../../../config";
import styles from "../../styles/Header.module.scss";

const HeaderComponent = () => {
  const { Header } = Layout;
  return (
    <Header className={styles.header}>
      <Link href="/">
        <a data-testid="logo">
          <Image
            src={Logo}
            height={48}
            className={styles.logo}
            alt="Sparrow Logo"
          />
        </a>
      </Link>
      <h1 data-testid="company-name" className={styles.headerText}>
        {config.companyName}
      </h1>
    </Header>
  );
};

export default HeaderComponent;
