/* eslint-disable max-classes-per-file */
import * as NotehubJs from "@blues-inc/notehub-js";
import AppService, { AppServiceInterface } from "./AppService";
import Config from "../../Config";
import { UrlManager } from "../components/presentation/UrlManager";
import { NextJsUrlManager } from "../adapters/nextjs/NextJsUrlManager";
import { DataProvider } from "./DataProvider";
import IDBuilder, { SimpleIDBuilder } from "./IDBuilder";
import NotehubDataProvider from "./notehub/NotehubDataProvider";
import { AttributeStore } from "./AttributeStore";
import NotehubAttributeStore from "./notehub/NotehubAttributeStore";

// ServiceLocator is the top-level consturction and dependency injection tool
// for server-side node code.
class ServiceLocatorServer {
  private appService?: AppServiceInterface;

  private urlManager?: UrlManager;

  private dataProvider?: DataProvider;

  private attributeStore?: AttributeStore;

  getAppService(): AppServiceInterface {
    if (!this.appService) {
      this.appService = new AppService(
        Config.hubFleetUID,
        new SimpleIDBuilder(),
        this.getDataProvider(),
        this.getAttributeStore()
      );
    }
    return this.appService;
  }

  private getDataProvider(): DataProvider {
    if (!this.dataProvider) {
      const projectID = IDBuilder.buildProjectID(Config.hubProjectUID);
      const fleetID = IDBuilder.buildFleetID(Config.hubFleetUID);
      const notehubJsClient = NotehubJs.ApiClient.instance;
      const notehubProvider = new NotehubDataProvider(
        projectID,
        fleetID,
        // todo remove when no longer in use
        Config.hubAuthToken,
        Config.hubClientId,
        Config.hubClientSecret,
        notehubJsClient
      );
      this.dataProvider = notehubProvider;
    }
    return this.dataProvider;
  }

  getAttributeStore(): AttributeStore {
    if (!this.attributeStore) {
      const projectID = IDBuilder.buildProjectID(Config.hubProjectUID);
      const notehubJsClient = NotehubJs.ApiClient.instance;
      const notehubStore: AttributeStore = new NotehubAttributeStore(
        projectID,
        Config.hubAuthToken,
        notehubJsClient
      );
      this.attributeStore = notehubStore;
    }
    return this.attributeStore;
  }

  getUrlManager(): UrlManager {
    if (!this.urlManager) {
      this.urlManager = NextJsUrlManager;
    }
    return this.urlManager;
  }
}

let Services: ServiceLocatorServer | null = null;

function services() {
  // Don’t create a ServiceLocator until it’s needed. This prevents all service
  // initialization steps from happening as soon as you import this module.
  if (!Services) {
    Services = new ServiceLocatorServer();
  }
  return Services;
}

// eslint-disable-next-line import/prefer-default-export
export { services };
