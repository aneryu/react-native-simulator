#import "PackagerConnection.h"

#include <folly/json.h>

#import <Foundation/Foundation.h>

#include <string>
#include <utility>

namespace {
NSString* nsString(const std::string& value) {
  NSString* string = [[NSString alloc] initWithBytes:value.data()
                                              length:value.size()
                                            encoding:NSUTF8StringEncoding];
  return string != nil ? string : @"";
}
} // namespace

@interface RNSPackagerSocket : NSObject <NSURLSessionWebSocketDelegate>
- (instancetype)initWithURL:(NSString*)url
                   onReload:(PackagerConnection::ReloadCallback)onReload;
- (void)start;
- (void)stop;
@end

class PackagerConnection::Impl {
 public:
  RNSPackagerSocket* socket{nil};
};

@implementation RNSPackagerSocket {
  NSString* _url;
  PackagerConnection::ReloadCallback _onReload;
  NSURLSession* _session;
  NSURLSessionWebSocketTask* _task;
  BOOL _stopped;
  BOOL _initialConnection;
  BOOL _loggedFailure;
}

- (instancetype)initWithURL:(NSString*)url
                   onReload:(PackagerConnection::ReloadCallback)onReload {
  self = [super init];
  if (self) {
    _url = [url copy];
    _onReload = std::move(onReload);
    _stopped = NO;
    _initialConnection = YES;
    _loggedFailure = NO;
  }
  return self;
}

- (void)start {
  [self connect];
}

- (void)stop {
  _stopped = YES;
  [_task cancel];
  _task = nil;
  [_session invalidateAndCancel];
  _session = nil;
}

- (void)connect {
  if (_stopped) {
    return;
  }
  NSURL* url = [NSURL URLWithString:_url];
  if (url == nil) {
    return;
  }
  [_task cancel];
  [_session invalidateAndCancel];
  NSURLSessionConfiguration* configuration =
      [NSURLSessionConfiguration defaultSessionConfiguration];
  configuration.timeoutIntervalForRequest = 5;
  _session = [NSURLSession sessionWithConfiguration:configuration
                                           delegate:self
                                      delegateQueue:nil];
  NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url];
  if (url.host.length > 0) {
    NSString* scheme = url.scheme.lowercaseString;
    if ([scheme isEqualToString:@"wss"]) {
      scheme = @"https";
    } else if ([scheme isEqualToString:@"ws"]) {
      scheme = @"http";
    }
    NSString* origin = url.port != nil
        ? [NSString stringWithFormat:@"%@://%@:%@", scheme, url.host, url.port]
        : [NSString stringWithFormat:@"%@://%@", scheme, url.host];
    [request setValue:origin forHTTPHeaderField:@"Origin"];
  }
  _task = [_session webSocketTaskWithRequest:request];
  [_task resume];
  [self armReceive];
}

- (void)scheduleReconnect {
  if (_stopped) {
    return;
  }
  __weak RNSPackagerSocket* weakSelf = self;
  dispatch_after(
      dispatch_time(DISPATCH_TIME_NOW, (int64_t)(5 * NSEC_PER_SEC)),
      dispatch_get_global_queue(QOS_CLASS_UTILITY, 0),
      ^{
        [weakSelf connect];
      });
}

- (void)armReceive {
  if (_stopped || _task == nil) {
    return;
  }
  __weak RNSPackagerSocket* weakSelf = self;
  [_task receiveMessageWithCompletionHandler:^(
             NSURLSessionWebSocketMessage* message, NSError* error) {
    RNSPackagerSocket* strongSelf = weakSelf;
    if (strongSelf == nil || strongSelf->_stopped) {
      return;
    }
    if (error != nil) {
      [strongSelf handleClosed];
      return;
    }
    if (message.type == NSURLSessionWebSocketMessageTypeString &&
        message.string.length > 0) {
      [strongSelf handleMessage:message.string];
    }
    [strongSelf armReceive];
  }];
}

- (void)handleMessage:(NSString*)text {
  try {
    auto json = folly::parseJson(std::string(text.UTF8String));
    if (!json.isObject() || json.getDefault("version") != 2) {
      return;
    }
    const auto method = json.getDefault("method");
    if (method == "reload" && _onReload) {
      _onReload();
    }
  } catch (const std::exception&) {
  }
}

- (void)handleClosed {
  if (_stopped) {
    return;
  }
  [self scheduleReconnect];
}

- (void)URLSession:(NSURLSession*)session
    webSocketTask:(NSURLSessionWebSocketTask*)task
    didOpenWithProtocol:(NSString*)protocol {
  (void)session;
  (void)task;
  (void)protocol;
  if (_stopped) {
    return;
  }
  if (!_initialConnection && _onReload) {
    _onReload();
  }
  _initialConnection = NO;
}

- (void)URLSession:(NSURLSession*)session
    webSocketTask:(NSURLSessionWebSocketTask*)task
    didCloseWithCode:(NSURLSessionWebSocketCloseCode)closeCode
              reason:(NSData*)reason {
  (void)session;
  (void)task;
  (void)closeCode;
  (void)reason;
  [self handleClosed];
}

- (void)URLSession:(NSURLSession*)session
              task:(NSURLSessionTask*)task
didCompleteWithError:(NSError*)error {
  (void)session;
  (void)task;
  if (error != nil && !_loggedFailure && _initialConnection) {
    _loggedFailure = YES;
  }
  [self handleClosed];
}
@end

std::unique_ptr<PackagerConnection> PackagerConnection::connect(
    std::string url,
    ReloadCallback onReload) {
  auto impl = std::make_shared<Impl>();
  impl->socket = [[RNSPackagerSocket alloc] initWithURL:nsString(url)
                                               onReload:std::move(onReload)];
  [impl->socket start];
  return std::unique_ptr<PackagerConnection>(
      new PackagerConnection(std::move(impl)));
}

PackagerConnection::PackagerConnection(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

PackagerConnection::~PackagerConnection() {
  [impl_->socket stop];
  impl_->socket = nil;
}
