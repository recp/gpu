#import <UIKit/UIKit.h>

#include <stdint.h>

#import "../../include/gpu/gpu.h"

int gpu_test_ray_query(GPUAdapter *adapter, const char *bytecodePath);

static GPUAdapter *
SelectAdapter(GPUInstance *instance) {
  GPUAdapter *adapter;
  uint32_t    count;
  GPUResult   result;

  adapter = NULL;
  count   = 1u;
  result  = GPUEnumerateAdapters(instance, &count, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !adapter) {
    return NULL;
  }
  return adapter;
}

@interface RayQueryViewController : UIViewController {
@private
  UILabel *_statusLabel;
  BOOL     _started;
}
@end

@implementation RayQueryViewController

- (void)setStatus:(NSString *)status color:(UIColor *)color {
  self.view.backgroundColor = color;
  _statusLabel.text         = status;
}

- (void)runCheck {
  NSURL *artifactURL;

  artifactURL = [NSBundle.mainBundle URLForResource:@"ray_query"
                                      withExtension:@"us"];
  if (!artifactURL) {
    [self setStatus:@"RAY QUERY ARTIFACT MISSING" color:UIColor.redColor];
    return;
  }

  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    GPUInstance *instance;
    GPUAdapter  *adapter;
    BOOL         supported;
    int          passed;

    instance = NULL;
    adapter  = NULL;
    supported = NO;
    passed    = 0;
    if (GPUCreateInstance(NULL, &instance) == GPU_OK && instance) {
      adapter   = SelectAdapter(instance);
      supported = adapter &&
        GPUIsFeatureSupported(adapter, GPU_FEATURE_RAY_QUERY);
      if (supported) {
        passed = gpu_test_ray_query(adapter,
                                    artifactURL.fileSystemRepresentation);
      }
    }
    GPUDestroyInstance(instance);

    dispatch_async(dispatch_get_main_queue(), ^{
      if (!supported) {
        NSLog(@"GPU: iOS ray query unsupported");
        [self setStatus:@"RAY QUERY UNSUPPORTED"
                  color:UIColor.systemYellowColor];
      } else if (passed) {
        NSLog(@"GPU: iOS ray query passed");
        [self setStatus:@"RAY QUERY PASS"
                  color:UIColor.systemGreenColor];
      } else {
        NSLog(@"GPU: iOS ray query failed");
        [self setStatus:@"RAY QUERY FAIL" color:UIColor.systemRedColor];
      }
    });
  });
}

- (void)viewDidLoad {
  [super viewDidLoad];
  self.view.backgroundColor = UIColor.darkGrayColor;

  _statusLabel = [UILabel new];
  _statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
  _statusLabel.font          = [UIFont monospacedSystemFontOfSize:24.0
                                                          weight:UIFontWeightBold];
  _statusLabel.text          = @"RAY QUERY RUNNING";
  _statusLabel.textColor     = UIColor.blackColor;
  _statusLabel.textAlignment = NSTextAlignmentCenter;
  [self.view addSubview:_statusLabel];
  [NSLayoutConstraint activateConstraints:@[
    [_statusLabel.centerXAnchor constraintEqualToAnchor:self.view.centerXAnchor],
    [_statusLabel.centerYAnchor constraintEqualToAnchor:self.view.centerYAnchor]
  ]];
}

- (void)viewDidAppear:(BOOL)animated {
  [super viewDidAppear:animated];
  if (!_started) {
    _started = YES;
    [self runCheck];
  }
}

@end

@interface RayQueryAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@end

@implementation RayQueryAppDelegate

- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
  RayQueryViewController *controller;

  (void)application;
  (void)launchOptions;
  controller = [RayQueryViewController new];
  self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
  self.window.rootViewController = controller;
  [self.window makeKeyAndVisible];
  return YES;
}

@end

int
main(int argc, char **argv) {
  @autoreleasepool {
    return UIApplicationMain(argc,
                             argv,
                             nil,
                             NSStringFromClass(RayQueryAppDelegate.class));
  }
}
