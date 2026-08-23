import 'package:secure_connectivity/secure_connectivity.dart';

import '../aquahub/api.dart';
import '../data/home_assistant_api.dart';

AppFailure mapAquaHubFailure(HubFailure failure) => AppFailure(
  code: switch (failure.type) {
    HubFailureType.network => AppFailureCode.offline,
    HubFailureType.authentication => AppFailureCode.authentication,
    HubFailureType.server => AppFailureCode.server,
    HubFailureType.invalidResponse => AppFailureCode.invalidResponse,
    HubFailureType.security => AppFailureCode.tls,
  },
  messageKey: switch (failure.type) {
    HubFailureType.network => 'errorNetwork',
    HubFailureType.authentication => 'errorToken',
    HubFailureType.server => 'errorServer',
    HubFailureType.invalidResponse => 'errorInvalidResponse',
    HubFailureType.security => 'errorCertificate',
  },
  statusCode: failure.statusCode,
  safeDetails: failure.message,
);

AppFailure mapHomeAssistantFailure(HomeAssistantFailure failure) => AppFailure(
  code: switch (failure.type) {
    HomeAssistantFailureType.authentication => AppFailureCode.authentication,
    HomeAssistantFailureType.network => AppFailureCode.offline,
    HomeAssistantFailureType.invalidResponse => AppFailureCode.invalidResponse,
    HomeAssistantFailureType.server => AppFailureCode.server,
  },
  messageKey: switch (failure.type) {
    HomeAssistantFailureType.authentication => 'errorToken',
    HomeAssistantFailureType.network => 'errorNetwork',
    HomeAssistantFailureType.invalidResponse => 'errorInvalidResponse',
    HomeAssistantFailureType.server => 'errorServer',
  },
  statusCode: failure.statusCode,
  safeDetails: failure.message,
);
