/* eslint-disable max-classes-per-file */
import AxiosHttpNotehubAccessor from "./notehub/AxiosHttpNotehubAccessor";
import AppService, { AppServiceInterface } from "./AppService";
import Config from "../../Config";
import { UrlManager } from "../components/presentation/UrlManager";
import { NextJsUrlManager } from "../adapters/nextjs/NextJsUrlManager";
import { NotehubAccessor } from "./notehub/NotehubAccessor";
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

  private notehubAccessor?: NotehubAccessor;

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
      const notehubProvider = new NotehubDataProvider(
        this.getNotehubAccessor(),
        projectID,
        fleetID
      );
      this.dataProvider = notehubProvider;
    }
    return this.dataProvider;
  }

  private getNotehubAccessor(): NotehubAccessor {
    if (!this.notehubAccessor) {
      this.notehubAccessor = new AxiosHttpNotehubAccessor(
        Config.hubBaseURL,
        Config.hubProjectUID,
        Config.hubAuthToken,
        Config.hubFleetUID
      );
    }
    return this.notehubAccessor;
  }

  getAttributeStore(): AttributeStore {
    if (!this.attributeStore) {
      const notehubStore: AttributeStore = new NotehubAttributeStore(
        this.getNotehubAccessor()
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
