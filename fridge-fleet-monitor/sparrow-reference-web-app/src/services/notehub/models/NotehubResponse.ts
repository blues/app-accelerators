import NotehubRoutedEvent from "./NotehubRoutedEvent";

interface NotehubResponse {
  events?: NotehubRoutedEvent[];
  has_more?: boolean;
  through?: string;
}

export default NotehubResponse;
