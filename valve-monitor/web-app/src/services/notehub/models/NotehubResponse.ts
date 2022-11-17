import NotehubEvent from "./NotehubEvent";

interface NotehubResponse {
  events?: NotehubEvent[];
  has_more?: boolean;
  through?: string;
}

export default NotehubResponse;
