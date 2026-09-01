#import "HeadlessWebSocket.h"

#include "HeadlessBlob.h"

#import <Foundation/Foundation.h>

#include <cctype>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace jsi = facebook::jsi;
namespace react = facebook::react;

namespace {
class HeadlessWebSocketModule;

NSString* nsString(const std::string& value) {
  NSString* string = [[NSString alloc] initWithBytes:value.data()
                                              length:value.size()
                                            encoding:NSUTF8StringEncoding];
  return string != nil ? string : @"";
}

std::string stdString(NSString* value) {
  if (value == nil) {
    return {};
  }
  const char* utf8 = value.UTF8String;
  return utf8 == nullptr ? std::string{} : std::string(utf8);
}

std::string trimCopy(std::string_view value) {
  size_t begin = 0;
  while (begin < value.size() &&
         std::isspace(static_cast<unsigned char>(value[begin]))) {
    ++begin;
  }
  size_t end = value.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return std::string(value.substr(begin, end - begin));
}

std::string joinedProtocols(const std::vector<std::string>& protocols) {
  std::string joined;
  for (const auto& protocol : protocols) {
    auto value = trimCopy(protocol);
    if (value.empty() || value.find(',') != std::string::npos) {
      continue;
    }
    if (!joined.empty()) {
      joined += ',';
    }
    joined += value;
  }
  return joined;
}

int intArg(
    const jsi::Value* args,
    size_t count,
    size_t index,
    int fallback = 0) {
  if (index >= count || !args[index].isNumber()) {
    return fallback;
  }
  return static_cast<int>(args[index].getNumber());
}

std::string stringArg(
    jsi::Runtime& runtime,
    const jsi::Value* args,
    size_t count,
    size_t index) {
  if (index >= count || !args[index].isString()) {
    return {};
  }
  return args[index].getString(runtime).utf8(runtime);
}
} // namespace

@interface HeadlessWebSocketConnection : NSObject <NSURLSessionWebSocketDelegate>
@property(nonatomic, assign) int socketId;
@property(nonatomic, strong) NSURLSession* session;
@property(nonatomic, strong) NSURLSessionWebSocketTask* task;

- (instancetype)initWithSocketId:(int)socketId
                          module:(std::shared_ptr<HeadlessWebSocketModule>)module;
- (BOOL)ownedBy:(const void*)module;
- (void)connectURL:(NSURL*)url
           headers:(const std::vector<std::pair<std::string, std::string>>&)headers
         protocols:(const std::string&)protocols;
- (void)sendText:(const std::string&)text;
- (void)sendBytes:(const std::string&)bytes;
- (void)ping;
- (void)closeWithCode:(int)code
               reason:(const std::string&)reason
                 emit:(BOOL)emit;
- (void)closeSilently;
@end

namespace {
NSMutableDictionary<NSNumber*, HeadlessWebSocketConnection*>* connections() {
  static NSMutableDictionary* dictionary;
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    dictionary = [NSMutableDictionary dictionary];
  });
  return dictionary;
}

HeadlessWebSocketConnection* connectionFor(int socketId) {
  @synchronized(connections()) {
    return connections()[@(socketId)];
  }
}

void retainConnection(int socketId, HeadlessWebSocketConnection* connection) {
  HeadlessWebSocketConnection* previous = nil;
  @synchronized(connections()) {
    previous = connections()[@(socketId)];
    connections()[@(socketId)] = connection;
  }
  if (previous != nil && previous != connection) {
    [previous closeSilently];
  }
}

void closeSocketsOwnedBy(void* module) {
  NSMutableArray<HeadlessWebSocketConnection*>* owned = [NSMutableArray array];
  @synchronized(connections()) {
    NSArray<NSNumber*>* keys = [connections() allKeys];
    for (NSNumber* key in keys) {
      HeadlessWebSocketConnection* connection = connections()[key];
      if ([connection ownedBy:module]) {
        [owned addObject:connection];
        [connections() removeObjectForKey:key];
      }
    }
  }
  for (HeadlessWebSocketConnection* connection in owned) {
    [connection closeSilently];
  }
}

class HeadlessWebSocketModule final
    : public react::TurboModule,
      public std::enable_shared_from_this<HeadlessWebSocketModule> {
 public:
  explicit HeadlessWebSocketModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("WebSocketModule", std::move(jsInvoker)) {
    methodMap_["connect"] = {4, &invokeConnect};
    methodMap_["send"] = {2, &invokeSend};
    methodMap_["sendBinary"] = {2, &invokeSendBinary};
    methodMap_["ping"] = {1, &invokePing};
    methodMap_["close"] = {3, &invokeClose};
    methodMap_["addListener"] = {1, &invokeNoop};
    methodMap_["removeListeners"] = {1, &invokeNoop};
  }

  ~HeadlessWebSocketModule() override {
    closeSocketsOwnedBy(this);
  }

  void emitOpen(int socketId, std::string protocol) {
    emitDeviceEvent(
        "websocketOpen",
        [socketId, protocol = std::move(protocol)](
            jsi::Runtime& runtime, std::vector<jsi::Value>& args) {
          jsi::Object event(runtime);
          event.setProperty(runtime, "id", jsi::Value(socketId));
          event.setProperty(
              runtime,
              "protocol",
              jsi::String::createFromUtf8(runtime, protocol));
          args.emplace_back(std::move(event));
        });
  }

  void emitClosed(int socketId, int code, std::string reason) {
    emitDeviceEvent(
        "websocketClosed",
        [socketId, code, reason = std::move(reason)](
            jsi::Runtime& runtime, std::vector<jsi::Value>& args) {
          jsi::Object event(runtime);
          event.setProperty(runtime, "id", jsi::Value(socketId));
          event.setProperty(runtime, "code", jsi::Value(code));
          event.setProperty(
              runtime,
              "reason",
              jsi::String::createFromUtf8(runtime, reason));
          args.emplace_back(std::move(event));
        });
  }

  void emitFailed(int socketId, std::string message) {
    emitDeviceEvent(
        "websocketFailed",
        [socketId, message = std::move(message)](
            jsi::Runtime& runtime, std::vector<jsi::Value>& args) {
          jsi::Object event(runtime);
          event.setProperty(runtime, "id", jsi::Value(socketId));
          event.setProperty(
              runtime,
              "message",
              jsi::String::createFromUtf8(runtime, message));
          args.emplace_back(std::move(event));
        });
  }

  void emitText(int socketId, std::string data) {
    emitDeviceEvent(
        "websocketMessage",
        [socketId, data = std::move(data)](
            jsi::Runtime& runtime, std::vector<jsi::Value>& args) {
          jsi::Object event(runtime);
          event.setProperty(runtime, "id", jsi::Value(socketId));
          event.setProperty(
              runtime,
              "type",
              jsi::String::createFromAscii(runtime, "text"));
          event.setProperty(
              runtime, "data", jsi::String::createFromUtf8(runtime, data));
          args.emplace_back(std::move(event));
        });
  }

  void emitBinary(int socketId, std::string bytes) {
    if (headlessBlobWebSocketHandlerEnabled(socketId)) {
      const int size = static_cast<int>(bytes.size());
      auto blobId = headlessBlobStore(std::move(bytes));
      emitDeviceEvent(
          "websocketMessage",
          [socketId, blobId = std::move(blobId), size](
              jsi::Runtime& runtime, std::vector<jsi::Value>& args) {
            jsi::Object data(runtime);
            data.setProperty(
                runtime,
                "blobId",
                jsi::String::createFromUtf8(runtime, blobId));
            data.setProperty(runtime, "offset", jsi::Value(0));
            data.setProperty(runtime, "size", jsi::Value(size));
            jsi::Object event(runtime);
            event.setProperty(runtime, "id", jsi::Value(socketId));
            event.setProperty(
                runtime,
                "type",
                jsi::String::createFromAscii(runtime, "blob"));
            event.setProperty(runtime, "data", std::move(data));
            args.emplace_back(std::move(event));
          });
      return;
    }
    auto encoded = headlessBlobBase64Encode(bytes);
    emitDeviceEvent(
        "websocketMessage",
        [socketId, encoded = std::move(encoded)](
            jsi::Runtime& runtime, std::vector<jsi::Value>& args) {
          jsi::Object event(runtime);
          event.setProperty(runtime, "id", jsi::Value(socketId));
          event.setProperty(
              runtime,
              "type",
              jsi::String::createFromAscii(runtime, "binary"));
          event.setProperty(
              runtime,
              "data",
              jsi::String::createFromUtf8(runtime, encoded));
          args.emplace_back(std::move(event));
        });
  }

 private:
  static HeadlessWebSocketModule& self(react::TurboModule& module) {
    return static_cast<HeadlessWebSocketModule&>(module);
  }

  static jsi::Value invokeNoop(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return jsi::Value::undefined();
  }

  static jsi::Value invokeConnect(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    auto owner = self(module).shared_from_this();
    const int socketId = intArg(args, count, 3);
    if (count < 4 || !args[0].isString()) {
      owner->emitFailed(socketId, "Invalid WebSocket URL");
      return jsi::Value::undefined();
    }
    auto url = args[0].getString(runtime).utf8(runtime);
    std::vector<std::string> protocols;
    if (args[1].isObject() && args[1].getObject(runtime).isArray(runtime)) {
      auto array = args[1].getObject(runtime).getArray(runtime);
      const auto length = array.size(runtime);
      protocols.reserve(length);
      for (size_t index = 0; index < length; ++index) {
        const auto value = array.getValueAtIndex(runtime, index);
        if (value.isString()) {
          protocols.push_back(value.getString(runtime).utf8(runtime));
        }
      }
    }
    std::vector<std::pair<std::string, std::string>> headers;
    if (args[2].isObject()) {
      auto options = args[2].getObject(runtime);
      const auto headersValue = options.getProperty(runtime, "headers");
      if (headersValue.isObject()) {
        auto headersObject = headersValue.getObject(runtime);
        auto names = headersObject.getPropertyNames(runtime);
        const auto nameCount = names.size(runtime);
        for (size_t index = 0; index < nameCount; ++index) {
          const auto nameValue = names.getValueAtIndex(runtime, index);
          if (!nameValue.isString()) {
            continue;
          }
          auto key = nameValue.getString(runtime).utf8(runtime);
          const auto value = headersObject.getProperty(
              runtime, jsi::PropNameID::forUtf8(runtime, key));
          if (value.isString()) {
            headers.emplace_back(
                std::move(key), value.getString(runtime).utf8(runtime));
          }
        }
      }
    }
    NSURL* nsUrl = [NSURL URLWithString:nsString(url)];
    if (url.empty() || nsUrl == nil || nsUrl.scheme.length == 0) {
      owner->emitFailed(socketId, "Invalid WebSocket URL");
      return jsi::Value::undefined();
    }
    auto connection =
        [[HeadlessWebSocketConnection alloc] initWithSocketId:socketId
                                                       module:owner];
    retainConnection(socketId, connection);
    [connection connectURL:nsUrl
                   headers:headers
                 protocols:joinedProtocols(protocols)];
    return jsi::Value::undefined();
  }

  static jsi::Value invokeSend(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    [connectionFor(intArg(args, count, 1))
        sendText:stringArg(runtime, args, count, 0)];
    return jsi::Value::undefined();
  }

  static jsi::Value invokeSendBinary(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    const int socketId = intArg(args, count, 1);
    auto encoded = stringArg(runtime, args, count, 0);
    auto bytes = headlessBlobBase64Decode(encoded);
    if (bytes.empty() && !encoded.empty()) {
      self(module).emitFailed(socketId, "invalid base64");
      return jsi::Value::undefined();
    }
    headlessWebSocketSendBinary(socketId, bytes);
    return jsi::Value::undefined();
  }

  static jsi::Value invokePing(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    [connectionFor(intArg(args, count, 0)) ping];
    return jsi::Value::undefined();
  }

  static jsi::Value invokeClose(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    [connectionFor(intArg(args, count, 2))
        closeWithCode:intArg(args, count, 0, 1000)
               reason:stringArg(runtime, args, count, 1)
                 emit:YES];
    return jsi::Value::undefined();
  }
};
} // namespace

@implementation HeadlessWebSocketConnection {
  std::weak_ptr<HeadlessWebSocketModule> _module;
  HeadlessWebSocketModule* _moduleRaw;
  BOOL _closed;
}

- (instancetype)initWithSocketId:(int)socketId
                          module:(std::shared_ptr<HeadlessWebSocketModule>)module {
  self = [super init];
  if (self) {
    _socketId = socketId;
    _module = module;
    _moduleRaw = module.get();
    _closed = NO;
  }
  return self;
}

- (BOOL)ownedBy:(const void*)module {
  return _moduleRaw == module;
}

- (BOOL)markClosed {
  if (_closed) {
    return NO;
  }
  _closed = YES;
  return YES;
}

- (void)cleanupResources {
  NSURLSession* session = self.session;
  self.task = nil;
  self.session = nil;
  @synchronized(connections()) {
    if (connections()[@(self.socketId)] == self) {
      [connections() removeObjectForKey:@(self.socketId)];
    }
  }
  headlessBlobRemoveWebSocketHandler(self.socketId);
  [session finishTasksAndInvalidate];
}

- (void)connectURL:(NSURL*)url
           headers:(const std::vector<std::pair<std::string, std::string>>&)headers
         protocols:(const std::string&)protocols {
  NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url];
  BOOL hasOrigin = NO;
  for (const auto& header : headers) {
    NSString* key = nsString(header.first);
    [request setValue:nsString(header.second) forHTTPHeaderField:key];
    if ([key caseInsensitiveCompare:@"origin"] == NSOrderedSame) {
      hasOrigin = YES;
    }
  }
  if (!hasOrigin) {
    NSString* scheme = url.scheme.lowercaseString;
    if ([scheme isEqualToString:@"wss"]) {
      scheme = @"https";
    } else if ([scheme isEqualToString:@"ws"]) {
      scheme = @"http";
    }
    if (url.host.length > 0 && scheme.length > 0) {
      NSString* origin = url.port != nil
          ? [NSString stringWithFormat:@"%@://%@:%@", scheme, url.host, url.port]
          : [NSString stringWithFormat:@"%@://%@", scheme, url.host];
      [request setValue:origin forHTTPHeaderField:@"Origin"];
    }
  }
  if (!protocols.empty()) {
    [request setValue:nsString(protocols)
        forHTTPHeaderField:@"Sec-WebSocket-Protocol"];
  }
  NSURLSessionConfiguration* configuration =
      [NSURLSessionConfiguration defaultSessionConfiguration];
  configuration.timeoutIntervalForRequest = 10;
  NSURLSession* session =
      [NSURLSession sessionWithConfiguration:configuration
                                    delegate:self
                               delegateQueue:nil];
  self.session = session;
  self.task = [session webSocketTaskWithRequest:request];
  [self.task resume];
  [self armReceive];
}

- (void)sendText:(const std::string&)text {
  if (_closed || self.task == nil) {
    return;
  }
  NSURLSessionWebSocketMessage* message =
      [[NSURLSessionWebSocketMessage alloc] initWithString:nsString(text)];
  __weak HeadlessWebSocketConnection* weakSelf = self;
  [self.task sendMessage:message
       completionHandler:^(NSError* error) {
         [weakSelf failIfNeeded:error];
       }];
}

- (void)sendBytes:(const std::string&)bytes {
  if (_closed || self.task == nil) {
    return;
  }
  NSData* data = [NSData dataWithBytes:bytes.data() length:bytes.size()];
  NSURLSessionWebSocketMessage* message =
      [[NSURLSessionWebSocketMessage alloc] initWithData:data];
  __weak HeadlessWebSocketConnection* weakSelf = self;
  [self.task sendMessage:message
       completionHandler:^(NSError* error) {
         [weakSelf failIfNeeded:error];
       }];
}

- (void)ping {
  if (_closed || self.task == nil) {
    return;
  }
  [self.task sendPingWithPongReceiveHandler:^(NSError* error) {
    (void)error;
  }];
}

- (void)closeWithCode:(int)code
               reason:(const std::string&)reason
                 emit:(BOOL)emit {
  if (_closed) {
    return;
  }
  NSString* reasonString = nsString(reason);
  NSData* reasonData = reason.empty()
      ? nil
      : [reasonString dataUsingEncoding:NSUTF8StringEncoding];
  // NSURLSession rejects reserved close codes; fall back to cancel.
  @try {
    [self.task cancelWithCloseCode:static_cast<NSURLSessionWebSocketCloseCode>(
                                       code)
                            reason:reasonData];
  } @catch (NSException*) {
    [self.task cancel];
  }
  if (![self markClosed]) {
    return;
  }
  if (emit) {
    [self emitClosedWithCode:code reason:reason];
  }
  [self cleanupResources];
}

- (void)closeSilently {
  if (![self markClosed]) {
    return;
  }
  [self.task cancel];
  [self cleanupResources];
}

- (void)failIfNeeded:(NSError*)error {
  if (error == nil) {
    return;
  }
  [self failWithMessage:stdString(error.localizedDescription)];
}

- (void)failWithMessage:(const std::string&)message {
  if (![self markClosed]) {
    return;
  }
  [self emitFailed:message];
  [self cleanupResources];
}

- (void)armReceive {
  if (_closed || self.task == nil) {
    return;
  }
  __weak HeadlessWebSocketConnection* weakSelf = self;
  [self.task receiveMessageWithCompletionHandler:^(
                 NSURLSessionWebSocketMessage* message, NSError* error) {
    HeadlessWebSocketConnection* strongSelf = weakSelf;
    if (strongSelf == nil || strongSelf->_closed) {
      return;
    }
    if (error != nil) {
      [strongSelf failIfNeeded:error];
      return;
    }
    if (message.type == NSURLSessionWebSocketMessageTypeString) {
      [strongSelf emitText:stdString(message.string)];
    } else {
      NSData* data = message.data;
      std::string bytes;
      if (data != nil && data.length > 0) {
        const auto* start = static_cast<const char*>(data.bytes);
        bytes.assign(start, start + data.length);
      }
      [strongSelf emitBinary:std::move(bytes)];
    }
    [strongSelf armReceive];
  }];
}

- (void)emitOpen:(NSString*)protocol {
  if (auto module = _module.lock()) {
    module->emitOpen(self.socketId, stdString(protocol));
  }
}

- (void)emitClosedWithCode:(int)code reason:(const std::string&)reason {
  if (auto module = _module.lock()) {
    module->emitClosed(self.socketId, code, reason);
  }
}

- (void)emitFailed:(const std::string&)message {
  if (auto module = _module.lock()) {
    module->emitFailed(self.socketId, message);
  }
}

- (void)emitText:(std::string)data {
  if (auto module = _module.lock()) {
    module->emitText(self.socketId, std::move(data));
  }
}

- (void)emitBinary:(std::string)bytes {
  if (auto module = _module.lock()) {
    module->emitBinary(self.socketId, std::move(bytes));
  }
}

- (void)URLSession:(NSURLSession*)session
      webSocketTask:(NSURLSessionWebSocketTask*)webSocketTask
didOpenWithProtocol:(NSString*)protocol {
  (void)session;
  (void)webSocketTask;
  if (_closed) {
    return;
  }
  [self emitOpen:protocol != nil ? protocol : @""];
}

- (void)URLSession:(NSURLSession*)session
      webSocketTask:(NSURLSessionWebSocketTask*)webSocketTask
   didCloseWithCode:(NSURLSessionWebSocketCloseCode)closeCode
             reason:(NSData*)reason {
  (void)session;
  (void)webSocketTask;
  if (![self markClosed]) {
    return;
  }
  std::string reasonText;
  if (reason != nil && reason.length > 0) {
    NSString* string = [[NSString alloc] initWithData:reason
                                             encoding:NSUTF8StringEncoding];
    reasonText = stdString(string);
  }
  [self emitClosedWithCode:static_cast<int>(closeCode) reason:reasonText];
  [self cleanupResources];
}

- (void)URLSession:(NSURLSession*)session
              task:(NSURLSessionTask*)task
didCompleteWithError:(NSError*)error {
  (void)session;
  (void)task;
  if (error == nil || _closed) {
    return;
  }
  [self failWithMessage:stdString(error.localizedDescription)];
}

@end

void headlessWebSocketSendBinary(int socketId, const std::string& bytes) {
  [connectionFor(socketId) sendBytes:bytes];
}

std::shared_ptr<react::TurboModule> createHeadlessWebSocketModule(
    std::shared_ptr<react::CallInvoker> jsInvoker) {
  return std::make_shared<HeadlessWebSocketModule>(std::move(jsInvoker));
}

void headlessWebSocketReset() {
  NSArray<HeadlessWebSocketConnection*>* sockets;
  @synchronized(connections()) {
    sockets = [connections() allValues];
    [connections() removeAllObjects];
  }
  for (HeadlessWebSocketConnection* socket in sockets) {
    [socket closeSilently];
  }
}
